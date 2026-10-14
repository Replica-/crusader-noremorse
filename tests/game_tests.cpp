// Milestone 1 checks on the original data: a saved game runs, and the camera
// can cross the whole map without usecode errors while the fast area follows.

#include "core/avi_movie.hpp"
#include "core/dtable.hpp"
#include "core/movie_subtitles.hpp"
#include "engine/palette_fader.hpp"
#include "engine/ui/computer_gump.hpp"
#include "engine/ui/keypad_gump.hpp"
#include "engine/ui/gump_keys.hpp"
#include "engine/world/actor_anim.hpp"
#include "engine/world/actor_create.hpp"
#include "engine/world/aim_mode.hpp"
#include "engine/usecode/world_intrinsics.hpp"
#include "engine/world/activity.hpp"
#include "engine/world/attack.hpp"
#include "engine/world/chargers.hpp"
#include "engine/world/collision.hpp"
#include "engine/world/gravity.hpp"
#include "engine/world/inventory.hpp"
#include "engine/world/item_selection.hpp"
#include "engine/world/pathfind.hpp"
#include "engine/world/player_commands.hpp"
#include "engine/world/sprite_process.hpp"
#include "engine/world/projectile.hpp"
#include "engine/world/teleport.hpp"
#include "game/game.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <tuple>

using namespace crusader;

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

constexpr std::uint32_t kFrameUs = 1'000'000 / 60;

using Key = std::tuple<std::int32_t, std::int32_t, std::int32_t, std::uint16_t, std::uint16_t>;

// Glob items on the map, by position and look; counts those outside the fast area.
std::multiset<Key> glob_items(const World& w, std::size_t& outside) {
    std::multiset<Key> keys;
    outside = 0;
    w.for_each([&](const Item& i) {
        if ((i.flags & 0x82) != 0x82 || i.parent || (i.flags & kItemEthereal)) return;
        keys.emplace(i.x, i.y, i.z, i.shape, i.frame);
        outside += !w.fast_area().contains(i.x, i.y);
    });
    return keys;
}

void test_load_and_run(const std::filesystem::path& data) {
    Game game(data);
    game.load_save(1);
    const Item* player = game.world().get(1);
    CHECK(player && game.world().state.camera_x == player->x && game.world().state.camera_y == player->y);

    // The view puts the camera 320 pixels from the left, 300 from the top.
    Surface8 screen(Game::kScreenWidth, Game::kScreenHeight);
    RenderStats stats;
    game.render(screen, &stats);
    CHECK(stats.drawn > 100 && stats.missing_shape == 0);

    for (int i = 0; i < 600; ++i) game.update(kFrameUs);  // 10 s
    CHECK(game.machine().faults().empty());
    CHECK(game.kernel().ticks() >= 599);
}

void test_player_walks(const std::filesystem::path& data) {
    // Keys as the keyboard interrupt sees them: E0-extended arrows.
    constexpr std::uint8_t kUp = 0x48, kRight = 0x4D;
    Game game(data);
    game.load_save(1);
    World& w = game.world();
    auto run = [&](int frames) {
        for (int i = 0; i < frames; ++i) game.update(kFrameUs);
    };
    const Item start = *w.get(1);
    const int dir = w.npc_direction(1) & ~1;  // walking has 8 directions
    CHECK(dir == 2);

    // Holding up: the KeyDaemon repeats the key, the player walks north-east
    // until the railing (shape 139 at y 15423) stops him.
    game.key(kUp, true, true, 0);
    run(120);
    const Item& now = *w.get(1);
    const int dx = now.x - start.x, dy = now.y - start.y;
    std::printf("player walked from (%d, %d) to (%d, %d), direction %d\n", start.x, start.y, now.x, now.y, dir);
    const int sx = kDirectionDx[dir], sy = kDirectionDy[dir];
    CHECK(dx * sy == dy * sx && dx > 0 && dy < 0);
    const SweepResult further = try_move(w, 1, {now.x + 2 * sx, now.y + 2 * sy, now.z},
                                         kSweepSupportOnly | kSweepStepUp);
    const Item* railing = w.get(further.blocker);
    CHECK(!further.ok && railing && railing->shape == 139);
    CHECK(railing && now.y - 3 * 16 + 1 == railing->y + 1);
    game.key(kUp, true, false, 0);
    run(30);

    // Right arrow: the stand animation turns him by 2 (8-direction action).
    game.key(kRight, true, true, 0);
    run(2);
    game.key(kRight, true, false, 0);
    run(30);
    CHECK(w.npc_direction(1) == 4);

    // Up again: he walks along +x, in half strides, showing walk frames.
    const Item before = *w.get(1);
    game.key(kUp, true, true, 0);
    run(90);
    const Item& walking = *w.get(1);
    CHECK(walking.x > before.x + 100 && walking.y == before.y);
    const AnimAction* walk = game.animations().action(walking.shape, static_cast<std::uint16_t>(ActorAction::kWalk));
    bool walk_frame = false;
    for (std::size_t f = 0; walk && f < walk->frame_count(); ++f) walk_frame |= walk->frame(4, f)->frame == walking.frame;
    CHECK(walk_frame);

    // Release: the current half stride ends, then he stands still.
    game.key(kUp, true, false, 0);
    run(60);
    const Item stopped = *w.get(1);
    run(30);
    CHECK(w.get(1)->x == stopped.x && w.get(1)->y == stopped.y);
    const AnimAction* stand = game.animations().action(stopped.shape, static_cast<std::uint16_t>(ActorAction::kStand));
    CHECK(stand && stand->frame(4, 0)->frame == stopped.frame);
    CHECK(w.cells_consistent());
    CHECK(game.machine().faults().empty());
}

void test_camera_follows(const std::filesystem::path& data) {
    // ITEMCACH.C 1D29: the view stays put while the player is inside the
    // margins, then jumps to bring him back to (320, 280) or to a side.
    Game game(data);
    game.load_save(1);
    World& w = game.world();
    const Item& p = *w.get(1);
    auto screen = [&](const Item& i) {
        const GameState& s = w.state;
        return std::pair<int, int>{((i.x - i.y) >> 1) - s.view_left, ((i.x + i.y) >> 2) - s.view_top - i.z};
    };
    std::printf("player on screen at (%d, %d)\n", screen(p).first, screen(p).second);
    // moveTo rounds each quarter (x >> 2) + (y >> 2), the projection the sum: 300 or 301.
    CHECK(screen(p).first == 320 && (screen(p).second == 300 || screen(p).second == 301));
    const std::int16_t left = w.state.view_left, top = w.state.view_top;
    int jumps = 0;
    for (int step = 0; step < 120; ++step) {
        const auto before = screen(p);
        w.move_item(1, p.x + 4, p.y, p.z);  // +x: down-right on screen
        if (w.state.view_top != top || w.state.view_left != left) {
            ++jumps;
            std::printf("camera jump at step %d: before (%d, %d), after (%d, %d)\n", step, before.first,
                        before.second, screen(p).first, screen(p).second);
            // Jumped when the player passed 409 (70 from the bottom); being
            // then right of 520, he is brought back to x 120.
            CHECK(before.second <= 409 && before.first + 2 > 520);
            CHECK(screen(p).second == 280 && screen(p).first == 120);
            break;
        }
        CHECK(screen(p).second <= 409);
    }
    CHECK(jumps == 1);
}

void test_camera_sweep(const std::filesystem::path& data) {
    Game game(data);
    game.load_save(1);
    World& w = game.world();
    const GameState start = w.state;
    std::size_t outside = 0;
    const auto initial = glob_items(w, outside);
    CHECK(!initial.empty() && outside == 0);

    // Bounds of the map's items.
    std::int32_t x0 = 0x7FFF, y0 = 0x7FFF, x1 = 0, y1 = 0;
    w.for_each([&](const Item& i) {
        if (i.parent || (i.flags & kItemEthereal)) return;
        x0 = std::min(x0, i.x), y0 = std::min(y0, i.y), x1 = std::max(x1, i.x), y1 = std::max(y1, i.y);
    });

    // Rows 1024 units apart, 64 units per frame along each row.
    std::size_t frames = 0, bad_positions = 0;
    for (std::int32_t y = y0; y <= y1; y += 1024) {
        for (std::int32_t x = x0; x <= x1; x += 64) {
            w.set_camera(x, y, start.camera_z);
            game.update(kFrameUs);
            ++frames;
            glob_items(w, outside);
            bad_positions += outside != 0;
        }
    }
    std::printf("map 1 sweep: x %d..%d, y %d..%d, %zu frames, %zu usecode faults, %zu processes at the end\n", x0, x1,
                y0, y1, frames, game.machine().faults().size(), game.kernel().process_count());
    for (const auto& f : game.machine().faults()) {
        std::fprintf(stderr, "  fault: %s (class 0x%04X ip 0x%04X)\n", f.message.c_str(), f.class_id, f.ip);
    }
    CHECK(game.machine().faults().empty());
    CHECK(bad_positions == 0);

    // Back to the start: the same glob items as in the saved game.
    w.set_camera(start.camera_x, start.camera_y, start.camera_z);
    const auto back = glob_items(w, outside);
    CHECK(back == initial);
}

}  // namespace

// Milestone 3: the status bar of SAVEGAME.13 (weapon with ammunition, an item
// in quantity, little energy).
void test_status_bar(const std::filesystem::path& data) {
    Game game(data);
    game.load_save(13);
    const World& w = game.world();
    const StatusBar& bar = game.status_bar();
    CHECK(w.state.status_bar_visible);
    for (int i = 0; i < StatusBar::kBoxes; ++i) {
        const auto& r = bar.box(i);
        CHECK(r.x0 == 15 + 127 * i && r.y0 == 450 && r.x1 == r.x0 + 107 && r.y1 == 477);
    }

    const auto c = bar.contents(w);
    const Item* weapon = w.get(c.weapon);
    const Item* ammo = w.get(c.ammo);
    const Item* item = w.get(c.item);
    std::printf("SAVEGAME.13 status: weapon %u (shape %u, q %u), ammo shape %u item %u (q %u), item shape %u q %u, "
                "hp %u -> %d, energy %u\n",
                c.weapon, weapon ? weapon->shape : 0, weapon ? weapon->quality : 0, c.ammo_shape, c.ammo,
                ammo ? ammo->quality : 0, item ? item->shape : 0, item ? item->quality : 0, w.npc_hp(1),
                c.health_width, w.npc_mana(1));
    CHECK(weapon && ammo && ammo->shape == c.ammo_shape && item && item->quality == 16);
    CHECK(c.health_width == static_cast<std::uint16_t>(w.npc_hp(1) * 66) / 150 && c.energy_width == w.npc_mana(1) * 66 / 10000);

    Surface8 screen(Game::kScreenWidth, Game::kScreenHeight);
    game.render(screen);
    const auto& health = bar.box(3);
    CHECK(screen.row(health.y0 + 7)[health.x0 + 34] == 0x41);
    CHECK(screen.row(health.y0 + 20)[health.x0 + 34 + c.health_width] == 0x41);
    CHECK(screen.row(health.y0 + 7)[health.x0 + 35 + c.health_width] != 0x41);

    game.world().state.status_bar_visible = false;  // CruStatusGump::I_hideStatusGump
    Surface8 bare(Game::kScreenWidth, Game::kScreenHeight);
    game.render(bare);
    CHECK(bare.row(health.y0 + 7)[health.x0 + 34] != 0x41);
}

// Milestone 3: key codes, cursor and the in-game menu.
void test_keys_and_cursor(const std::filesystem::path& data) {
    GameKeyEvent e;
    e.pressed = true;
    e.ascii = 0x1B;
    e.scancode = 0x01;
    CHECK(gump_key_code(e) == gump_key::kEscape);
    e.ascii = 0;
    e.scancode = 0x48;
    CHECK(gump_key_code(e) == gump_key::kUp);
    e.scancode = 0x2D;
    e.flags = kLeftAlt | kAlt;
    CHECK(gump_key_code(e) == gump_key::kAltX);
    e.flags = kCtrl | kAlt;
    CHECK(gump_key_code(e) == 0x42D);
    e.flags = kLeftShift;
    e.ascii = 'X';
    CHECK(gump_key_code(e) == 'X');
    e.pressed = false;
    CHECK(gump_key_code(e) == 0);

    auto c = MouseCursor::load(data / "STATIC/MOUSE.SHP");
    CHECK(c.frame_count() == 40 && !c.visible());
    // Directions around (320, 200), 0 = up, clockwise; zone 2 beyond 30 x 25.
    CHECK(MouseCursor::direction(320, 100) == 0 && MouseCursor::direction(420, 100) == 1);
    CHECK(MouseCursor::direction(420, 200) == 2 && MouseCursor::direction(420, 300) == 3);
    CHECK(MouseCursor::direction(320, 300) == 4 && MouseCursor::direction(220, 300) == 5);
    CHECK(MouseCursor::direction(220, 200) == 6 && MouseCursor::direction(220, 100) == 7);
    CHECK(MouseCursor::zone(350, 225) == 1 && MouseCursor::zone(351, 200) == 2 && MouseCursor::zone(10, 10) == 2);
    c.move_to(420, 200);
    CHECK(c.frame() == 2 + 8);  // directional, running zone
    c.push(MouseCursor::kArrow);
    CHECK(c.frame() == 0x24 && c.level() == 1);
    c.reset(MouseCursor::kMenuArrow);
    CHECK(c.level() == 0 && c.frame() == 0x25 && c.pop() == 0x25 && c.level() == 0);
    c.set_bottom(439);
    c.move_to(700, 470);
    CHECK(c.x() == 639 && c.y() == 439);
}

void test_menu(const std::filesystem::path& data) {
    Game game(data);
    game.load_save(1);
    for (int i = 0; i < 60; ++i) game.update(kFrameUs);
    CHECK(game.cursor().bottom() == 439 && !game.cursor().visible());
    const Palette before = game.palette();
    // The saved game's music (entry 70): track 1.
    CHECK(game.audio() && game.audio()->music.current() == 1 && game.audio()->mixer.music_playing());

    // Escape: the menu opens over a frozen world.
    game.key(0x01, false, true, 0, 0x1B);
    game.key(0x01, false, false, 0, 0);
    CHECK(game.menu() && game.cursor().visible() && game.cursor().frame() == 0x25);
    CHECK(!game.world().state.status_bar_visible && game.cursor().bottom() == 479);
    CHECK(!(game.palette() == before));
    CHECK(game.audio()->music.current() == Music::kMenu);
    const Item* player = game.world().get(1);
    const auto px = player->x, py = player->y;
    game.key(0x48, true, true, 0);  // up arrow: in the menu, not the player
    for (int i = 0; i < 120; ++i) game.update(kFrameUs);
    game.key(0x48, true, false, 0);
    CHECK(player->x == px && player->y == py);

    Surface8 screen(Game::kScreenWidth, Game::kScreenHeight);
    game.render(screen);
    CHECK(screen.row(0)[0] == 0);  // cleared outside the 638 x 240 area

    // Keys move the selection; right goes 3 forward from the left column.
    const int start = game.menu()->selected();
    game.key(0x50, true, true, 0);  // down
    game.key(0x50, true, false, 0);
    CHECK(game.menu()->selected() == std::min(start + 1, 5));

    // Mouse: press on "quit", drag out and back, release inside.
    const MainMenu::Rect quit = game.menu()->button(MainMenu::kQuit);
    game.mouse_button(true, quit.x0 + 2, quit.y0 + 2);
    CHECK(game.menu()->selected() == MainMenu::kQuit);
    game.mouse_move(10, 10);
    CHECK(game.menu()->selected() == MainMenu::kQuit);

    // Escape again: back to the game, the status bar returns.
    game.mouse_button(false, 10, 10);  // released outside: nothing
    CHECK(game.menu() != nullptr);
    game.key(0x01, false, true, 0, 0x1B);
    CHECK(!game.menu() && !game.quit_requested() && game.world().state.status_bar_visible);
    CHECK(!game.cursor().visible() && game.cursor().bottom() == 439 && game.palette() == before);
    CHECK(game.audio()->music.current() == 1);
    game.update(kFrameUs);
    CHECK(game.machine().faults().empty());

    // Quit through the mouse.
    game.key(0x01, false, true, 0, 0x1B);
    game.mouse_button(true, quit.x0 + 2, quit.y0 + 2);
    game.mouse_button(false, quit.x0 + 2, quit.y0 + 2);
    CHECK(!game.menu() && game.quit_requested());
}

void press(Game& game, std::uint8_t scancode, bool extended, std::uint8_t ascii = 0) {
    game.key(scancode, extended, true, 0, ascii);
    game.key(scancode, extended, false, 0, 0);
}

// Milestone 3: "load game" from the menu.
void test_load_from_menu(const std::filesystem::path& data) {
    const auto names = LoadScreen::read_names(data / "SAVEGAME");
    CHECK(names[0] == "SEB" && names[1] == "SEB2" && names[5] == "MISSION2" && names[7].empty() &&
          names[12].empty());

    Game reference(data);
    reference.load_save(2);
    const Item* expected = reference.world().get(1);

    Game game(data);
    game.load_save(1);
    game.update(kFrameUs);
    press(game, 0x01, false, 0x1B);  // Escape: menu, button 0 selected
    press(game, 0x50, true);         // down: "load"
    CHECK(game.menu() && game.menu()->selected() == MainMenu::kLoad);
    press(game, 0x1C, false, 0x0D);  // Enter: load screen
    CHECK(game.menu() && game.menu()->load_screen());

    Surface8 screen(Game::kScreenWidth, Game::kScreenHeight);
    game.render(screen);
    const auto& r = game.menu()->load_screen()->slot_rect(0);
    CHECK(r.x0 == 183 && r.y0 == 109 && r.x1 == 183 + 313 && r.y1 == 109 + 16);
    CHECK(screen.row(r.y0)[r.x0] == LoadScreen::kSelectedColour);  // slot 0 outlined

    // An empty slot (SAVEGAME.8 does not exist) cannot be chosen.
    const auto empty = game.menu()->load_screen()->slot_rect(7);
    game.mouse_button(true, empty.x0 + 5, empty.y0 + 5);
    game.mouse_button(false, empty.x0 + 5, empty.y0 + 5);
    CHECK(game.menu() && game.menu()->load_screen() && game.menu()->load_screen()->slot() == 7);
    press(game, 0x01, false, 0x1B);  // Escape: back to the menu
    CHECK(game.menu() && !game.menu()->load_screen());

    press(game, 0x1C, false, 0x0D);  // "load" again, slot 7 still selected
    for (int i = 0; i < 6; ++i) press(game, 0x48, true);  // up to slot 1
    CHECK(game.menu()->load_screen()->slot() == 1);
    press(game, 0x1C, false, 0x0D);  // choose SAVEGAME.2
    CHECK(!game.menu() && !game.quit_requested());
    game.update(kFrameUs);
    const Item* player = game.world().get(1);
    CHECK(player && expected && player->x == expected->x && player->y == expected->y && player->z == expected->z);
    CHECK(game.world().state.status_bar_visible && game.machine().faults().empty());
}

// Milestone 6: a new game from the fixed data (GAMEPAL_1028_0000), the
// teleport eggs and changing maps.
void test_new_game(const std::filesystem::path& data) {
    // Globals of a saved game: entry 60.
    {
        Game game(data);
        game.load_save(13);
        const auto save = FlexArchive::open(data / "SAVEGAME/SAVEGAME.13");
        const auto entry = save.entry(60);
        const auto& g = game.machine().globals();
        CHECK(entry.size() == 0x42 && std::equal(entry.begin(), entry.end(), g.begin()));
        CHECK(game.world().current_map() == 5 && game.world().state.keycards != 0);
    }

    Game game(data);
    game.new_game(0);
    World& w = game.world();
    std::ifstream in(data / "USECODE/UNKDS.DAT", std::ios::binary);
    const std::vector<std::uint8_t> unkds{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    CHECK(unkds.size() == 0x42 && std::equal(unkds.begin(), unkds.end(), game.machine().globals().begin()));
    CHECK(w.state.difficulty == 3 && w.npc_hp(1) == 150 && w.state.battery == 0);
    // The datalink in item slot 11, active, quality 1; the container first.
    const Item* player = w.get(1);
    CHECK(player && player->shape == 1 && player->contents.size() == 2);
    const ObjId datalink = w.npc_equip(1, 11);
    const Item* d = w.get(datalink);
    CHECK(d && d->shape == 0x4D4 && d->z == 11 && (d->flags & 0x0200) && d->quality == 1 && d->parent == 1);
    CHECK(w.npc_word(1, kNpcActiveItem) == 11);
    CHECK(player && w.get(player->contents[0])->shape == 0x598 && player->contents[1] == datalink);
    CHECK(w.current_map() == -1);

    // First slice: map 1 loaded, the player on egg 0x1E (the start pad).
    game.update(kFrameUs);
    CHECK(w.current_map() == 1 && w.cells_consistent());
    player = w.get(1);
    CHECK(player->x == 6399 && player->y == 6399 && player->z == 0 && player->map_number == 1);
    // Items read in file order from id 0x100 (records with shape 0 skipped).
    const auto records = read_fixed_map(FlexArchive::open(data / "STATIC/FIXED.DAT"), 1);
    const Item* first = w.get(kFirstItemId + 2);  // after the datalink and the container
    CHECK(first && first->shape == records[0].shape && first->x == records[0].x && first->y == records[0].y);

    // Mission 1's egg (family 4, type 0x0D: class 0x90D MISS1EGG) caches in
    // with the map and plays the briefing, 01A.AVI, after a jump to black:
    // the fade to grey ends at once and Escape, held, stops the movie.
    CHECK(game.movie() && game.movie()->kind() == MovieKind::kCutscene && game.movie()->screen().width == 640);
    game.key(0x01, false, true, 0, 0x1B);
    int frames = 0;
    for (; game.movie() && frames < 200; ++frames) game.update(kFrameUs);
    game.key(0x01, false, false, 0);
    CHECK(!game.movie() && frames < 5);

    // The trigger egg next to the pad (0x63, frame 0) sends him to the
    // mission's start, egg 0x63 at frame 1.
    for (int i = 0; i < 60; ++i) game.update(kFrameUs);
    player = w.get(1);
    const ObjId arrival = find_teleport_egg(w, 0x63);
    CHECK(arrival && player->x == w.get(arrival)->x && player->y == w.get(arrival)->y);
    CHECK(w.state.camera_x == player->x && w.state.camera_y == player->y);
    // MISS1EGG's arrival scene (class 0xA11, event 0x23) holds the player
    // in stasis (DS:7E10) until it ends.
    CHECK(w.state.avatar_in_stasis);
    CHECK(game.machine().faults().empty());
    // Then the script fades the game palette in (fadeToGamePal): its waiting
    // process resumes the colour cycling, and the stasis ends (0x076).
    CHECK(w.state.cru_stasis);
    int fading = 0;
    for (; w.state.cycle_paused && fading < 300; ++fading) game.update(kFrameUs);
    game.update(kFrameUs);
    std::printf("new game: colour cycling resumed after %d frames\n", fading);
    CHECK(!w.state.cycle_paused && !w.state.cru_stasis && game.palette() == w.state.game_palette);
    int scene = 0;
    for (; w.state.avatar_in_stasis && scene < 300; ++scene) game.update(kFrameUs);
    std::printf("new game: stasis over %d frames later\n", scene);
    CHECK(!w.state.avatar_in_stasis && player_may_act(w));
    CHECK(game.machine().unbound_intrinsic_calls().empty());

    // MainActor::I_switchMap(3) (mission 2): every process ends, map 3 replaces map 1;
    // the player keeps his inventory.
    start_switch_map(game.kernel(), w, 3);
    for (int i = 0; i < 3; ++i) game.update(kFrameUs);
    player = w.get(1);
    CHECK(w.current_map() == 3 && player->map_number == 3 && w.cells_consistent());
    const ObjId start = find_teleport_egg(w, kTeleportStartEgg);
    CHECK(start && player->x == w.get(start)->x && player->y == w.get(start)->y);
    CHECK(w.get(datalink) && w.get(datalink)->parent == 1);
    // From the menu: "new game" opens the difficulty screen (level 3 at
    // first, DIFF.PAL); Escape goes back, the level is kept; a click on
    // level 1 starts a new game at that level.
    {
        Game g(data);
        g.load_save(1);
        press(g, 0x01, false, 0x1B);
        CHECK(g.menu() && g.menu()->selected() == MainMenu::kNewGame);
        const Palette menu_palette = g.palette();
        press(g, 0x1C, false, 0x0D);
        const DifficultyScreen* ds = g.menu() ? g.menu()->difficulty_screen() : nullptr;
        CHECK(ds && ds->selected() == 3 && !(g.palette() == menu_palette));
        if (ds) {
            CHECK(ds->button(1).x0 == 159 && ds->button(1).y0 == 120 && ds->button(4).x1 == 479 && ds->button(4).y0 == 297);
        }
        press(g, 0x50, true);  // down
        CHECK(ds && ds->selected() == 4);
        press(g, 0x01, false, 0x1B);
        CHECK(g.menu() && !g.menu()->difficulty_screen() && g.palette() == menu_palette);
        press(g, 0x1C, false, 0x0D);
        ds = g.menu() ? g.menu()->difficulty_screen() : nullptr;
        CHECK(ds && ds->selected() == 4);
        if (ds) {
            const auto b = ds->button(1);
            g.mouse_button(true, b.x0 + 10, b.y0 + 5);
            g.mouse_button(false, b.x0 + 10, b.y0 + 5);
        }
        CHECK(!g.menu());
        g.update(kFrameUs);  // the new game, then its first slice: the start pad
        CHECK(g.world().state.difficulty == 1 && g.world().current_map() == 1 && g.world().get(1)->x == 6399);
    }

    // Map 1's items are gone: the lowest ids are map 3's first records.
    const auto records2 = read_fixed_map(FlexArchive::open(data / "STATIC/FIXED.DAT"), 3);
    first = w.get(kFirstItemId + 2);
    CHECK(first && first->shape == records2[0].shape && first->x == records2[0].x && first->y == records2[0].y);
    CHECK(game.machine().faults().empty());
}

// Milestone 6: cutscenes (PLAYER.C, segment 1468; subtitles, segment 1470).
void test_movies(const std::filesystem::path& data) {
    CHECK(movie_base_name("1a") == "01A" && movie_base_name("mva5b") == "MVA05B" &&
          movie_base_name("mva10") == "MVA10" && movie_base_name("16a") == "16A" && movie_base_name("r01") == "R01");

    // 01A.AVI: every frame decodes, its pixels within the movie's colours.
    const AviMovie m = AviMovie::load(data / "FLICS/01A.AVI");
    CHECK(m.width() == 240 && m.height() == 180 && m.codec() == fourcc("RRV2") && m.strips() == 15 &&
          m.frame_count() == 940 && m.colours() == 236 && m.has_audio() && m.audio_initial_frames() == 4);
    Surface8 frame(m.width(), m.height());
    int frames = 0;
    for (const AviMovie::Chunk& c : m.chunks()) {
        if (c.id != AviMovie::kFrameChunk && c.id != AviMovie::kHalfWidthChunk) continue;
        for (int s = 0; s < m.strips(); ++s) decode_movie_strip(m.data(c), s, m.strips(), false, frame);
        ++frames;
    }
    const auto [lo, hi] = std::minmax_element(frame.pixels.begin(), frame.pixels.end());
    CHECK(frames == 940 && *lo >= 10 && *hi <= 245);
    // WEC.AVI (RRV1, 5 strips): only half-width frames.
    const AviMovie wec = AviMovie::load(data / "FLICS/WEC.AVI");
    CHECK(wec.strips() == 5 && wec.rrv() && wec.width() == 320);
    Surface8 wide(wec.width(), wec.height());
    for (const AviMovie::Chunk& c : wec.chunks()) {
        if (c.id == AviMovie::kFrameChunk) CHECK(false);
        if (c.id != AviMovie::kHalfWidthChunk) continue;
        for (int s = 0; s < wec.strips(); ++s) decode_movie_strip(wec.data(c), s, wec.strips(), true, wide);
    }

    // 01A.TXT: 15 entries; the first has no text.
    const FontSet fonts = FontSet::load(data / "STATIC/FONTS.FLX");
    const auto subtitles = load_movie_subtitles(data / "FLICS/01A.TXT", fonts, MoviePlayer::kSubtitleFont);
    CHECK(subtitles.size() == 15 && subtitles[0].delay == 105 && !subtitles[0].text);
    CHECK(subtitles.size() == 15 && subtitles[1].delay == 65 && subtitles[1].text &&
          subtitles[1].text->rfind("Je suis le g\x82n\x82ral Maxis.", 0) == 0 && subtitles[1].width > 300 &&
          subtitles[14].delay == -875);

    // From SAVEGAME.1, small movie with subtitles: the game is frozen, the
    // screen fades to grey (40 Hz), then 15 frames a second.
    Game game(data);
    game.load_save(1);
    GameConfig config = game.config();
    config.big_video = false;
    config.subtitles = true;
    game.set_config(config);
    const Palette game_palette = game.palette();
    const std::uint32_t ticks = game.kernel().ticks();
    game.play_movie("nothing", MovieKind::kCutscene);
    CHECK(!game.movie());
    game.play_movie("1a", MovieKind::kCutscene);
    CHECK(game.movie() && game.palette() == game_palette);
    int fade = 0;
    for (; game.movie() && game.movie()->frame() == 0 && fade < 300; ++fade) game.update(kFrameUs);
    const auto vga = game.palette().to_vga();
    CHECK(fade > 30 && fade < 150 && vga[0] == 2 && vga[3 * 10] == reduce8(m.colour_table()[10].r) && vga[3 * 8] == 63);
    for (int i = 0; i < 600; ++i) game.update(kFrameUs);  // 10 s
    const MoviePlayer* p = game.movie();
    CHECK(p && p->frame() > 145 && p->frame() < 155 && game.kernel().ticks() == ticks);
    if (p) {
        // Centred at (200, 150), the background in grey levels, the
        // subtitle under it (frame 107 on: entry 2) at y 338.
        const Surface8& s = p->screen();
        const std::uint8_t inside = s.row(240)[320], outside = s.row(20)[20];
        CHECK(inside >= 10 && inside <= 245 && (outside < 8 || outside >= 248));
        int text = 0;
        for (int y = 338; y < 338 + 16; ++y) {
            for (int x = 20; x < 621; ++x) text += s.row(y)[x] != 0;
        }
        CHECK(text > 100);
    }
    game.key(0x01, false, true, 0, 0x1B);
    game.update(kFrameUs);
    game.key(0x01, false, false, 0);
    // The colour cycling, paused during the movie, resumes.
    CHECK(!game.movie() && game.palette() == game_palette && !game.world().state.cycle_paused);
    game.update(kFrameUs);
    game.update(kFrameUs);
    CHECK(game.kernel().ticks() > ticks);

    // An overlay keeps the game palette and has 13 strips. After intrinsic
    // 0x092 (DS:5FFE), the cycling stays paused when the movie ends.
    game.world().state.movie_keeps_cycle_paused = true;
    const Palette before_overlay = game.palette();
    game.play_movie("mva5b", MovieKind::kOverlay);
    for (int i = 0; i < 120; ++i) game.update(kFrameUs);
    CHECK(game.movie() && game.palette() == before_overlay && game.movie()->frame() > 25);
    game.key(0x01, false, true, 0, 0x1B);
    game.update(kFrameUs);
    game.key(0x01, false, false, 0);
    CHECK(!game.movie() && game.world().state.cycle_paused);
    CHECK(game.machine().faults().empty());
}

// Milestone 3: palette fades (FADE.C), one step per tick at 60 Hz.
void test_options(const std::filesystem::path& data) {
    using O = OptionsScreen;
    Game game(data);
    game.load_save(1);
    // A CRUSADER.CFG as installed (the data's may have been changed by
    // playing): no volume keys (defaults), "bigvideo=true".
    game.set_config(GameConfig::parse("irq=7\r\nsoundcard=3\r\nbigvideo=true\r\n"));
    CHECK(game.config().sound_volume == 0x40 && game.config().music_volume == 0x20 && game.config().big_video);
    const auto cfg = std::filesystem::temp_directory_path() / "crusader_options_test.cfg";
    std::filesystem::remove(cfg);
    game.set_config_file(cfg);
    game.update(kFrameUs);

    // In play, '-' and '=' step the music volume (CRUSADER_1138_0896).
    press(game, 0x0C, false, '-');
    CHECK(game.config().music_volume == 0x1F && game.audio()->mixer.music_volume() == 0x1F);
    press(game, 0x0D, false, '=');
    CHECK(game.config().music_volume == 0x20);

    press(game, 0x01, false, 0x1B);  // Escape: the menu
    press(game, 0x4D, true);         // right: "options"
    press(game, 0x1C, false, 0x0D);  // Enter
    const O* o = game.menu() ? game.menu()->options() : nullptr;
    CHECK(o != nullptr);
    if (!o) return;
    CHECK(o->focus() == 0 && o->sound_step() == 22 && o->music_step() == 15);
    CHECK(o->chosen(O::kAnimationOn) && !o->chosen(O::kAnimationOff) && o->chosen(O::kVideoBig));
    CHECK(o->chosen(O::kFilterOn) && o->chosen(O::kFrameSkipOff) && o->chosen(O::kBlastsOff));
    const auto quit = o->rect(O::kQuit);
    CHECK(quit.x0 == 517 && quit.y0 == 318);

    Surface8 screen(Game::kScreenWidth, Game::kScreenHeight);
    game.render(screen);
    CHECK(screen.row(145)[22] == 0x0C && screen.row(176)[258] == 0x0C);  // line 0 outlined

    // Keys: down to the sound volume, left lowers it one step, right at the
    // top changes nothing.
    for (int i = 0; i < 5; ++i) press(game, 0x50, true);
    CHECK(o->focus() == 5);
    press(game, 0x4B, true);
    CHECK(o->sound_step() == 21 && game.audio()->mixer.sound_volume() == 0x3D);
    press(game, 0x4D, true);
    press(game, 0x4D, true);
    CHECK(o->sound_step() == 22 && game.audio()->mixer.sound_volume() == 0x40);

    // Mouse: "music -" held for one second acts at once, then every 11 ticks.
    const auto minus = o->rect(O::kMusicDown);
    game.mouse_button(true, minus.x0 + 3, minus.y0 + 3);
    CHECK(o->focus() == 6 && o->music_step() == 14 && o->pressed(O::kMusicDown));
    for (int i = 0; i < 60; ++i) game.update(kFrameUs);
    CHECK(o->music_step() == 9 && game.audio()->mixer.music_volume() == O::kMusicSteps[9]);
    game.mouse_move(10, 10);  // dragged out: no more repeats
    for (int i = 0; i < 30; ++i) game.update(kFrameUs);
    CHECK(o->music_step() == 9 && !o->pressed(O::kMusicDown));
    game.mouse_button(false, 10, 10);

    // A two-choice widget acts on the press.
    const auto filter_off = o->rect(O::kFilterOff);
    game.mouse_button(true, filter_off.x0 + 3, filter_off.y0 + 3);
    game.mouse_button(false, filter_off.x0 + 3, filter_off.y0 + 3);
    CHECK(o->focus() == 4 && o->chosen(O::kFilterOff) && !game.config().filter);
    press(game, 0x34, false, '4');  // '4': the first widget of the line
    CHECK(game.config().filter);
    press(game, 0x36, false, '6');
    CHECK(!game.config().filter);

    // Down to "quit" (selected), Enter: back to the menu, the file written.
    for (int i = 0; i < 6; ++i) press(game, 0x50, true);
    CHECK(o->focus() == O::kQuitLine && o->pressed(O::kQuit));
    press(game, 0x1C, false, 0x0D);
    CHECK(game.menu() && !game.menu()->options());
    const GameConfig saved = GameConfig::load(cfg);
    CHECK(saved.music_volume == O::kMusicSteps[9] && saved.sound_volume == 0x40 && !saved.filter);
    CHECK(saved.big_video && saved.mouse_speed == 2 && saved.text().find("irq=7\r\n") == 0);
    std::filesystem::remove(cfg);

    // The focus stays on "quit" for the next time.
    press(game, 0x1C, false, 0x0D);
    CHECK(game.menu()->options() && game.menu()->options()->focus() == O::kQuitLine);
    press(game, 0x01, false, 0x1B);
    CHECK(game.menu() && !game.menu()->options());
    CHECK(game.machine().faults().empty());
}

void test_fades(const std::filesystem::path& data) {
    Game game(data);
    game.load_save(1);
    GameState& s = game.world().state;

    // The colour cycling runs once a game is loaded: entries 1..7 of the game
    // palette rotate, 8..14 change colour, and 1..14 go to the DAC.
    CHECK(!s.cycle_paused);
    const auto file = s.game_palette.to_vga();
    for (int i = 0; i < 31; ++i) game.update(kFrameUs);  // 15 ticks at 30 Hz
    auto cycled = s.game_palette.to_vga();
    int rotation = -1;
    for (int k = 0; k < 7 && rotation < 0; ++k) {
        bool same = true;
        for (int e = 1; e <= 7; ++e) {
            for (int c = 0; c < 3; ++c) same &= cycled[3 * e + c] == file[3 * ((e - 1 + k) % 7 + 1) + c];
        }
        if (same) rotation = k;
    }
    std::printf("fades: cycling rotated entries 1..7 by %d, entry 10 = (%d, %d, %d)\n", rotation, cycled[30],
                cycled[31], cycled[32]);
    CHECK(rotation > 0);
    CHECK(cycled[31] == 0 && cycled[32] == 0 && cycled[30] != 31);   // entry 10: red only, from 31
    CHECK(std::equal(cycled.begin() + 45, cycled.end(), file.begin() + 45));
    auto dac = s.screen_palette.to_vga();
    CHECK(std::equal(dac.begin() + 3, dac.begin() + 45, cycled.begin() + 3));

    // fadeToBlack(rate): the cycling pauses first and stays paused.
    const auto start = s.screen_palette.to_vga();
    int brightest = 0;
    for (auto c : start) brightest = std::max<int>(brightest, c);
    s.cycle_paused = true;
    const Pid fade = game.kernel().add(std::make_unique<PaletteFaderProcess>(
        s.screen_palette, Palette::filled(0, 0, 0), 60, 0, 1, &s.cycle_paused, false));
    for (int i = 0; i < 10; ++i) game.update(kFrameUs);
    const auto after10 = s.screen_palette.to_vga();
    bool stepped = true;
    for (std::size_t i = 0; i < start.size(); ++i) stepped &= after10[i] == std::max(0, start[i] - 10);
    CHECK(stepped);
    for (int i = 0; i < brightest; ++i) game.update(kFrameUs);
    CHECK(s.screen_palette == Palette::filled(0, 0, 0));
    // Over (its pid may already serve another process).
    const Process* p = game.kernel().get(fade);
    CHECK((!dynamic_cast<const PaletteFaderProcess*>(p) || p->terminated()) && s.cycle_paused);

    // A count of 5 ends the fade after 4 writes, then its flag +0x41
    // (fadeToBlack(rate, count)) resumes the cycling, which writes 1..14.
    game.kernel().add(std::make_unique<PaletteFaderProcess>(s.screen_palette, Palette::filled(63, 63, 63), 60, 5, 1,
                                                            &s.cycle_paused, true));
    for (int i = 0; i < 20; ++i) game.update(kFrameUs);
    dac = s.screen_palette.to_vga();
    cycled = s.game_palette.to_vga();
    CHECK(!s.cycle_paused);
    CHECK(std::all_of(dac.begin() + 45, dac.end(), [](std::uint8_t c) { return c == 4; }));
    CHECK(std::equal(dac.begin() + 3, dac.begin() + 45, cycled.begin() + 3));

    // Rate 0 writes at once. fadeToGamePal's waiting process resumes the
    // cycling once the fade is over.
    game.kernel().add(std::make_unique<PaletteFaderProcess>(s.screen_palette, s.game_palette, 0, 0));
    CHECK(s.screen_palette == s.game_palette);
    s.cycle_paused = true;  // jumpToAllBlack
    s.screen_palette = Palette::filled(0, 0, 0);
    const Pid in = game.kernel().add(
        std::make_unique<PaletteFaderProcess>(s.screen_palette, s.game_palette, 60, 0, 1, &s.cycle_paused, false));
    const Pid wait = game.kernel().add(std::make_unique<FadeWaitProcess>(in, s.cycle_paused));
    for (int i = 0; i < 10; ++i) game.update(kFrameUs);
    CHECK(s.cycle_paused && game.kernel().get(wait) && !game.kernel().get(wait)->terminated());
    for (int i = 0; i < 70; ++i) game.update(kFrameUs);
    CHECK(!s.cycle_paused && s.screen_palette == s.game_palette);
}

// Milestone 3: the credits from the menu.
void test_credits(const std::filesystem::path& data) {
    const auto cd = CreditsData::load(data / "STATIC");
    CHECK(cd.background.width == 640 && cd.background.height == 480 && cd.script.size() == 3601);

    Game game(data);
    game.load_save(1);
    press(game, 0x01, false, 0x1B);  // menu
    press(game, 0x4D, true);         // right: options
    press(game, 0x50, true);         // down: credits
    CHECK(game.menu() && game.menu()->selected() == MainMenu::kCredits);
    press(game, 0x1C, false, 0x0D);
    game.update(kFrameUs);
    CHECK(!game.cursor().visible());

    // The first page fades in: "REALISATEUR" / "TONY ZUROVEC" at y 180.
    for (int i = 0; i < 30; ++i) game.update(kFrameUs);
    CHECK(game.audio()->music.current() == Music::kCredits);
    Surface8 screen(Game::kScreenWidth, Game::kScreenHeight);
    game.render(screen);
    int changed = 0;
    for (int y = 170; y < 230; ++y)
        for (int x = 0; x < 640; ++x) changed += screen.row(y)[x] != cd.background.row(y)[x];
    CHECK(changed > 200);
    CHECK(game.palette() == cd.palette);

    // To the end (about 2 minutes), then back to the menu.
    int frames = 0;
    Credits* c = nullptr;
    const MainMenu* m = game.menu();
    for (; frames < 60 * 600; ++frames) {
        game.update(kFrameUs);
        if (!m) break;
        if (!const_cast<MainMenu*>(m)->credits()) break;
        c = const_cast<MainMenu*>(m)->credits();
    }
    std::printf("credits ran %d frames\n", frames);
    CHECK(c != nullptr && frames > 60 * 30 && frames < 60 * 600);
    CHECK(game.menu() && game.cursor().visible() && !(game.palette() == cd.palette));
    CHECK(game.audio()->music.current() == Music::kMenu);

    // A key stops them.
    press(game, 0x1C, false, 0x0D);
    game.update(kFrameUs);
    CHECK(const_cast<MainMenu*>(game.menu())->credits() != nullptr);
    press(game, 0x39, false, ' ');
    game.update(kFrameUs);
    CHECK(const_cast<MainMenu*>(game.menu())->credits() == nullptr);
    game.update(kFrameUs);
    CHECK(game.audio()->music.current() == Music::kMenu);
}

// Milestone 4: firing the shotgun of SAVEGAME.13 (shape 818, 1 round, 16 clips).
void settle(Game& game) {
    for (int i = 0; i < 600; ++i) {
        game.update(kFrameUs);
        if (!actor_busy(game.kernel(), 1) && !game.combat().shot_under_way()) return;
    }
}

std::uint16_t last_action(Game& game) {
    const auto r = game.world().npc_record(1);
    return static_cast<std::uint16_t>(r[npc::kLastAnim] | r[npc::kLastAnim + 1] << 8);
}

void test_firing(const std::filesystem::path& data) {
    // Action remapping for the shotgun (record +0xC = 0, overlay 0x33B).
    Game game(data);
    game.load_save(13);
    World& w = game.world();
    Item* weapon = w.get(w.active_weapon(1));
    CHECK(weapon && weapon->shape == 818);
    CHECK(remap_action(w, 1, 8) == 0x36 && remap_action(w, 1, 0x2A) == 0x32 && remap_action(w, 1, 7) == 0xC);
    CHECK(remap_action(w, 1, 1) == 1);
    const StatusBar& bar = game.status_bar();
    const Item* clips = w.get(bar.contents(w).ammo);
    CHECK(clips && clips->quality == 16 && weapon->quality == 1);

    settle(game);
    const std::uint16_t before = last_action(game);
    std::printf("firing: last action at start 0x%X\n", before);
    auto tap = [&] {
        press(game, 0x39, false, ' ');
        settle(game);
    };
    if (before == 0 || before == 1) {
        tap();  // raise the weapon: action 7 -> 0xC
        CHECK(last_action(game) == 0xC);
    }
    // Pump (action 0xA -> 0xF), with the shotgun's reload sound. The
    // conveyors of this map, started by their scripts (CONV_EL event 10,
    // Item::I_equip), keep the one-shot voices busy: the sound may be taken
    // over before the action ends, so it is checked meanwhile.
    const int reload_sfx = dtable::weapon(weapon->shape).reload_sfx();
    const int shot_sfx = dtable::weapon(weapon->shape).sound();
    std::printf("firing: shotgun sounds: shot 0x%X, reload 0x%X\n", shot_sfx, reload_sfx);
    press(game, 0x39, false, ' ');
    bool reload_heard = false;
    for (int i = 0; i < 600; ++i) {
        game.update(kFrameUs);
        reload_heard |= game.audio() && game.audio()->sources.is_playing(reload_sfx, 1);
        if (!actor_busy(game.kernel(), 1) && !game.combat().shot_under_way()) break;
    }
    CHECK(last_action(game) == 0xF && weapon->quality == 1 && game.shots_fired() == 0);
    CHECK(reload_heard);
    // NPC 6 (activity 0, default activity 2 = 5) stands on screen beside the
    // player, out of the line of fire: the shot wakes it (combat, target).
    const Item* player = w.get(1);
    w.move_item(6, player->x - 120, player->y + 120, player->z);
    CHECK(w.is_partly_on_screen(*w.get(6)) && !in_combat(w, 6) && w.npc_word(6, npc::kActivity) == 0);
    tap();  // shot (8 -> 0x36): one round
    std::printf("firing: NPC 6 after the shot: activity %u, in combat %d, target %u\n", w.npc_word(6, npc::kActivity),
                in_combat(w, 6), w.npc_word(6, npc::kTarget));
    CHECK(w.npc_word(6, npc::kActivity) == 5 && in_combat(w, 6) && w.npc_word(6, npc::kTarget) == 1);
    std::printf("firing: after the shot last action 0x%X, rounds %u, shot frames %d\n", last_action(game),
                weapon->quality, game.shots_fired());
    CHECK(last_action(game) == 0x36 && weapon->quality == 0 && game.shots_fired() >= 1);
    CHECK(game.audio() && game.audio()->sources.is_playing(shot_sfx, 1));
    tap();  // empty: reload from a clip (12 rounds), action 0xF
    CHECK(weapon->quality == 12 && clips->quality == 15 && last_action(game) == 0xF);
    CHECK(game.machine().faults().empty());
}

// Milestone 6.2: the inventory keys of World_on_key (player_commands.hpp).
void test_inventory_keys(const std::filesystem::path& data) {
    Game game(data);
    game.load_save(13);
    World& w = game.world();
    settle(game);
    auto slot_shape = [&](int slot) {
        const Item* i = w.get(w.npc_equip(1, slot));
        return i ? i->shape : 0;
    };
    std::printf("inventory keys: weapons");
    for (int s = 1; s <= 5; ++s) std::printf(" %d:0x%X", s, slot_shape(s));
    std::printf(", items");
    for (int s = 11; s <= 25; ++s) {
        if (slot_shape(s)) std::printf(" %d:0x%X", s, slot_shape(s));
    }
    std::printf("; active weapon %u, item %u\n", w.npc_word(1, kNpcActiveWeapon), w.npc_word(1, kNpcActiveItem));

    // W then Q: the next weapon slot holding something, then back.
    std::vector<int> weapons;
    for (int s = 1; s <= 5; ++s) {
        if (slot_shape(s)) weapons.push_back(s);
    }
    const int weapon0 = w.npc_word(1, kNpcActiveWeapon);
    CHECK(weapons.size() >= 2 && weapon0 != 0);
    const auto at = std::find(weapons.begin(), weapons.end(), weapon0);
    const int next = at + 1 == weapons.end() ? weapons.front() : *(at + 1);
    press(game, 0x11, false, 'w');
    CHECK(w.npc_word(1, kNpcActiveWeapon) == next);
    press(game, 0x10, false, 'q');
    CHECK(w.npc_word(1, kNpcActiveWeapon) == weapon0);
    press(game, 0x37, false, '*');  // keypad *, through the Controller
    CHECK(w.npc_word(1, kNpcActiveWeapon) == next);
    press(game, 0x10, false, 'q');

    // O then I through the item slots.
    std::vector<int> items;
    for (int s = 11; s <= 24; ++s) {
        if (slot_shape(s)) items.push_back(s);
    }
    const int item0 = w.npc_word(1, kNpcActiveItem);
    if (items.size() >= 2 && item0 != 0) {
        const auto it = std::find(items.begin(), items.end(), item0);
        const int next_item = it == items.end() || it + 1 == items.end() ? items.front() : *(it + 1);
        press(game, 0x18, false, 'o');
        CHECK(w.npc_word(1, kNpcActiveItem) == next_item);
        press(game, 0x17, false, 'i');
        CHECK(w.npc_word(1, kNpcActiveItem) == item0);
        press(game, 0x4A, false, '-');  // keypad -
        CHECK(w.npc_word(1, kNpcActiveItem) == next_item);
        press(game, 0x17, false, 'i');
    }

    // M: a medikit (given if none is carried) heals 80 hit points and is
    // used up.
    ObjId medikit = 0;
    for (int s = 0; s < 30; ++s) {
        if (slot_shape(s) == 0x351) medikit = w.npc_equip(1, s);
    }
    if (!medikit) {
        medikit = w.create(0x351, 0);
        w.pop_to_coords(0, 0, 0);
        CHECK(add_item_cru(w, medikit, false));
    }
    const std::uint16_t count = w.get(medikit)->quality;
    w.set_npc_word(1, 0x00, 50);
    press(game, 0x32, false, 'm');
    for (int i = 0; i < 30; ++i) game.update(kFrameUs);
    std::printf("inventory keys: medikits %u, hit points 50 -> %u\n", count, w.npc_hp(1));
    CHECK(w.npc_hp(1) == 130);
    if (count > 1) CHECK(w.get(medikit) && w.get(medikit)->quality == count - 1);
    else CHECK(!w.get(medikit) && std::none_of(items.begin(), items.end(), [&](int s) { return w.npc_equip(1, s) == medikit; }));

    // Ctrl+D: the weapon goes to the player's feet, out of its slot; the
    // next weapon is active.
    const ObjId dropped = w.active_weapon(1);
    const int dropped_slot = w.npc_word(1, kNpcActiveWeapon);
    game.key(0x20, false, true, kCtrl, 4);
    game.key(0x20, false, false, kCtrl, 0);
    const Item* gun = w.get(dropped);
    const Item* player = w.get(1);
    CHECK(gun && gun->parent == 0 && !(gun->flags & (kItemEquipped | kItemContained)));
    CHECK(gun && gun->x == player->x && gun->y == player->y && gun->z == player->z);
    CHECK(w.npc_equip(1, dropped_slot) == 0 && w.npc_word(1, kNpcActiveWeapon) != dropped_slot);
    CHECK(std::find(player->contents.begin(), player->contents.end(), dropped) == player->contents.end());

    // In stasis nothing happens.
    w.state.avatar_in_stasis = true;
    const int weapon1 = w.npc_word(1, kNpcActiveWeapon);
    press(game, 0x11, false, 'w');
    CHECK(w.npc_word(1, kNpcActiveWeapon) == weapon1);
    w.state.avatar_in_stasis = false;

    // Tab: a jump (0x3A), after putting the weapon away when in a combat
    // stance; the player lands further on.
    for (int i = 0; i < 120 && actor_busy(game.kernel(), 1); ++i) game.update(kFrameUs);
    const Item* p = w.get(1);
    const int x0 = p->x, y0 = p->y;
    bool jumped = false;
    press(game, 0x0F, false, '\t');
    for (int i = 0; i < 240; ++i) {
        game.update(kFrameUs);
        jumped = jumped || last_action(game) == 0x3A;
    }
    const int moved = std::abs(p->x - x0) + std::abs(p->y - y0);
    std::printf("inventory keys: Tab jump %d, moved %d\n", jumped, moved);
    CHECK(jumped && moved >= 64);
    // J while running (Shift + up): the run gives way to a running jump (0x38).
    game.key(0x48, true, true, kLeftShift);
    for (int i = 0; i < 90; ++i) game.update(kFrameUs);  // 0x22 / 0x23, then the run
    const std::uint16_t before = last_action(game);
    game.key(0x24, false, true, kLeftShift, 'J');
    game.key(0x24, false, false, kLeftShift, 0);
    bool running_jump = false;
    for (int i = 0; i < 20; ++i) {
        game.update(kFrameUs);
        running_jump = running_jump || last_action(game) == 0x38;
    }
    game.key(0x48, true, false, 0);
    for (int i = 0; i < 120; ++i) game.update(kFrameUs);
    std::printf("inventory keys: J after action 0x%X: running jump %d\n", before, running_jump);
    CHECK((before == 3 || before == 0x30 || before == 0x31) && running_jump);
    CHECK(game.machine().faults().empty());
    for (const auto& [n, calls] : game.machine().unbound_intrinsic_calls())
        std::printf("inventory keys: unbound intrinsic 0x%03X, %u calls\n", n, calls);
    CHECK(game.machine().unbound_intrinsic_calls().empty());
}

// Milestone 6.2: the item selection mode (item_selection.hpp).
void test_item_selection(const std::filesystem::path& data) {
    {
        // SAVEGAME.1: one selectable item next to the player.
        Game game(data);
        game.load_save(1);
        World& w = game.world();
        settle(game);
        press(game, 0x1F, false, 's');
        const auto& sel = w.state.selection;
        std::printf("selection (save 1): active %d,", sel.active);
        for (ObjId id : sel.items) std::printf(" %u:0x%X", id, w.get(id)->shape);
        std::printf("\n");
        CHECK(sel.active && sel.items.size() == 1 && selected_item(w) == sel.items[0]);
        press(game, 0x01, false, 0x1B);  // Escape ends the mode, the menu stays closed
        CHECK(!sel.active && !game.menu());
    }
    Game game(data);
    game.load_save(13);
    World& w = game.world();
    settle(game);
    const auto& sel = w.state.selection;
    // Nothing near the player: the mode does not start.
    press(game, 0x1F, false, 's');
    CHECK(!sel.active && sel.items.empty());
    // Three items at 0x20, 0x40 and 0x90 (out of range) from the player's centre.
    const Item* player = w.get(1);
    auto drop = [&](std::uint16_t shape, int dx) {
        const ObjId id = w.create(shape, 0);
        w.pop_to_coords(player->x + dx, player->y, player->z);
        return id;
    };
    const ObjId far_item = drop(0x582, 0x40);
    const ObjId near_item = drop(0x351, 0x20);
    drop(0x351, 0x90);
    press(game, 0x1F, false, 's');
    CHECK(sel.active && sel.items.size() == 2 && selected_item(w) == near_item);
    press(game, 0x34, false, '.');
    CHECK(selected_item(w) == far_item);
    press(game, 0x34, false, '.');  // wraps
    CHECK(selected_item(w) == near_item);
    press(game, 0x33, false, ',');  // wraps backwards
    CHECK(selected_item(w) == far_item);
    // Enter picks the energy cube up.
    press(game, 0x1C, false, 0x0D);
    const Item* cube = w.get(far_item);
    CHECK(!sel.active && (!cube || cube->parent == 1));
    // An arrow ends the mode (the player turns, an animation: also ends it).
    press(game, 0x1F, false, 's');
    CHECK(sel.active);
    game.key(0x4B, true, true, 0);
    game.key(0x4B, true, false, 0);
    CHECK(!sel.active);
    // G picks everything in range, one per frame (the player carries 10
    // medikits, the most: down to 1 first).
    for (int s = 0; s < 30; ++s) {
        if (Item* i = w.get(w.npc_equip(1, s)); i && i->shape == 0x351) i->quality = 1;
    }
    const ObjId near2 = drop(0x351, 0x10);
    press(game, 0x22, false, 'g');
    CHECK(sel.active && sel.pick_all);
    for (int i = 0; i < 5; ++i) game.update(kFrameUs);
    const Item* m1 = w.get(near_item);
    const Item* m2 = w.get(near2);
    std::printf("selection: after G, active %d, medikits %d %d\n", sel.active, m1 ? static_cast<int>(m1->parent) : -1,
                m2 ? static_cast<int>(m2->parent) : -1);
    CHECK(!sel.active && (!m1 || m1->parent == 1) && (!m2 || m2->parent == 1));
    CHECK(game.machine().faults().empty());
}

// Milestone 6.2: R and the message box (message_gump.hpp).
void test_message_box(const std::filesystem::path& data) {
    Game game(data);
    game.load_save(1);
    World& w = game.world();
    settle(game);
    const std::uint8_t level = game.kernel().level();
    CHECK(w.state.target_designation);  // DS:2889 is 1 at start-up
    press(game, 0x13, false, 'r');
    const MessageGump* box = game.message();
    CHECK(!w.state.target_designation && box && box->text() == "DESIGNATION DE CIBLE INACTIVE.");
    CHECK(game.kernel().level() == level + 1);
    if (!box) return;
    MessageGump::Rect r = box->box();
    MessageGump::Rect b = box->button();
    std::printf("message box: (%d, %d)-(%d, %d), button (%d, %d)-(%d, %d)\n", r.x0, r.y0, r.x1, r.y1, b.x0, b.y0, b.x1,
                b.y1);
    // Centred on the screen; the button centred across it, 4 pixels above its bottom.
    CHECK(r.x0 + ((r.x1 - r.x0 + 1) >> 1) == 320 && r.y0 + ((r.y1 - r.y0 + 1) >> 1) == 240);
    CHECK(b.y1 == r.y1 - 4 && b.x0 - r.x0 == ((r.x1 - r.x0 + 1) >> 1) - ((b.x1 - b.x0 + 1) >> 1));
    // The game is frozen: the player's position does not change.
    const Item* player = w.get(1);
    const auto x = player->x, y = player->y;
    game.key(0x48, true, true, 0);  // up arrow held: closes the box at once...
    game.key(0x48, true, false, 0);
    game.update(kFrameUs);
    CHECK(!game.message() && game.kernel().level() == level);
    // ...but a box opened again ignores the walk.
    press(game, 0x13, false, 'r');
    CHECK(w.state.target_designation && game.message() && game.message()->text() == "DESIGNATION DE CIBLE ACTIVE.");
    for (int i = 0; i < 30; ++i) game.update(kFrameUs);
    CHECK(player->x == x && player->y == y && game.message());
    if (!game.message()) return;
    r = game.message()->box();
    b = game.message()->button();
    Surface8 screen(Game::kScreenWidth, Game::kScreenHeight);
    game.render(screen);
    // Outline 0, fill 0x14 inside the shade, light 0x16 along the bottom.
    CHECK(screen.row(r.y0)[r.x0] == 0 && screen.row(r.y1)[r.x1] == 0);
    CHECK(screen.row(r.y0 + 2)[r.x0 + 2] == MessageGump::kColour);
    CHECK(screen.row(r.y0 + 1)[r.x0 + 1] == MessageGump::kColour - 2);
    CHECK(screen.row(r.y1 - 1)[r.x0 + 2] == MessageGump::kColour + 2);
    CHECK(screen.row(b.y0 + 2)[b.x0 + 2] == MessageGump::kColour + 2);
    // The mouse: pressing then releasing outside "Ok" leaves it; inside closes it.
    game.mouse_button(true, b.x0 + 2, b.y0 + 2);
    game.mouse_button(false, r.x0 + 1, r.y0 + 1);
    CHECK(game.message());
    game.mouse_button(true, b.x0 + 2, b.y0 + 2);
    game.mouse_button(false, b.x0 + 3, b.y0 + 3);
    game.update(kFrameUs);
    CHECK(!game.message() && game.kernel().level() == level);
    // The flag outlives a load.
    press(game, 0x13, false, 'r');
    press(game, 0x1C, false, 0x0D);
    game.load_save(1);
    CHECK(!game.world().state.target_designation);
    CHECK(game.machine().faults().empty());
}

// Milestone 6.2: the function keys of the game view (World_on_key).
void test_function_keys(const std::filesystem::path& data) {
    Game game(data);
    game.load_save(1);
    World& w = game.world();
    settle(game);
    const std::uint8_t level = game.kernel().level();

    // F1: the help, over a cleared screen with HELP1.DAT's palette.
    press(game, 0x3B, false);
    const auto* help = dynamic_cast<const HelpScreen*>(game.screen());
    CHECK(help && help->page() == 0 && game.kernel().level() == level + 1);
    CHECK(!w.state.status_bar_visible && game.cursor().visible());
    if (!help) return;
    CHECK(game.palette() == *help->palette() && !(game.palette() == w.state.game_palette));
    press(game, 0x4D, true);  // right: page 1
    CHECK(game.screen() == help && help->page() == 1);
    press(game, 0x4B, true);  // left: page 0
    press(game, 0x4B, true);  // left again: stays on page 0
    CHECK(help->page() == 0);
    // The hidden page: e - - -; any key then goes back to page 0's number.
    press(game, 0x12, false, 'e');
    for (int i = 0; i < 3; ++i) press(game, 0x0C, false, '-');
    CHECK(help->page() == HelpScreen::kHiddenPage);
    press(game, 0x4D, true);  // right: 4 -> 0 -> 1
    CHECK(help->page() == 1);
    press(game, 0x31, false, 'n');  // next from page 1 closes
    CHECK(!game.screen() && game.kernel().level() == level);
    CHECK(w.state.status_bar_visible && !game.cursor().visible() && game.palette() == w.state.game_palette);
    // '?' opens it too; the buttons: "next", then "quit".
    press(game, 0x35, false, '?');
    help = dynamic_cast<const HelpScreen*>(game.screen());
    CHECK(help != nullptr);
    if (!help) return;
    const ShapeButton::Rect next = help->buttons()[0].rect;
    const ShapeButton::Rect quit = help->buttons()[1].rect;
    std::printf("help buttons: (%d, %d)-(%d, %d), (%d, %d)-(%d, %d)\n", next.x0, next.y0, next.x1, next.y1, quit.x0,
                quit.y0, quit.x1, quit.y1);
    auto click = [&](const ShapeButton& b) {
        // An opaque pixel of the button.
        for (int y = b.rect.y0; y <= b.rect.y1; ++y) {
            for (int x = b.rect.x0; x <= b.rect.x1; ++x) {
                if (b.contains(x, y)) {
                    game.mouse_button(true, x, y);
                    game.mouse_button(false, x, y);
                    return;
                }
            }
        }
    };
    click(help->buttons()[0]);
    CHECK(game.screen() == help && help->page() == 1);
    click(help->buttons()[1]);
    CHECK(!game.screen());

    // Alt+X: the question 0x15; 'n' leaves, 'o' quits.
    game.key(0x2D, false, true, kAlt, 'x');
    game.key(0x2D, false, false, kAlt, 0);
    const auto* ask = dynamic_cast<const ShapeDialog*>(game.screen());
    CHECK(ask && ask->shape() == ShapeDialog::kQuitQuestion);
    if (!ask) return;
    const ShapeButton::Rect d = ask->rect();
    std::printf("quit question: (%d, %d)-(%d, %d)\n", d.x0, d.y0, d.x1, d.y1);
    CHECK(ask->buttons().size() == 2 && ask->buttons()[0].rect.x0 == d.x0 + 57 && ask->buttons()[0].rect.y0 == d.y0 + 47);
    press(game, 0x31, false, 'n');
    CHECK(!game.screen() && !game.quit_requested() && w.state.status_bar_visible);
    game.key(0x2D, false, true, kAlt, 'x');
    press(game, 0x18, false, 'o');
    CHECK(!game.screen() && game.quit_requested());

    // F2: the options screen, Escape closes it.
    press(game, 0x3C, false);
    CHECK(game.screen() && !game.screen()->over_view());
    press(game, 0x01, false, 0x1B);
    CHECK(!game.screen() && game.palette() == w.state.game_palette);

    // F8: the load screen; Escape closes it, a slot chosen loads that game.
    press(game, 0x42, false);
    CHECK(game.screen() && !game.screen()->over_view());
    press(game, 0x01, false, 0x1B);
    CHECK(!game.screen());

    // In the item selection mode the function keys do nothing.
    press(game, 0x1F, false, 's');
    if (w.state.selection.active) {
        press(game, 0x3B, false);
        CHECK(!game.screen());
        press(game, 0x01, false, 0x1B);
    }

    // F4: load the quick save (SAVEGAME.13), then the notice.
    press(game, 0x3E, false);
    ask = dynamic_cast<const ShapeDialog*>(game.screen());
    CHECK(ask && ask->shape() == ShapeDialog::kQuickLoadQuestion);
    press(game, 0x15, false, 'y');
    CHECK(!game.screen());
    game.update(kFrameUs);
    ask = dynamic_cast<const ShapeDialog*>(game.screen());
    CHECK(ask && ask->shape() == ShapeDialog::kQuickLoadNotice && !game.world().state.status_bar_visible);
    press(game, 0x1C, false, 0x0D);
    CHECK(!game.screen() && game.world().state.status_bar_visible);
    CHECK(game.machine().faults().empty());
}

// Milestone 5: the healing and energy stations (CruHealer, BatteryCharger),
// as their scripts start them, with their sounds.
// Milestone 6.2: the mouse in play (world_mouse.hpp).
void test_mouse_in_play(const std::filesystem::path& data) {
    Game game(data);
    game.load_save(1);
    World& w = game.world();
    settle(game);
    auto run = [&](int frames) {
        for (int i = 0; i < frames; ++i) game.update(kFrameUs);
    };
    const Item* player = w.get(1);
    CHECK(game.mouse_in_play() && !game.cursor().visible());
    GameConfig config = game.config();
    config.mouse_speed = 2;  // the default (data/CRUSADER.CFG has 3)
    game.set_config(config);

    // The item selection: motion does not put the mouse back; a left press
    // takes the selected item.
    press(game, 0x1F, false, 's');
    CHECK(w.state.selection.active);
    game.mouse_move(100, 100);
    run(1);
    CHECK(game.cursor().x() == 100);
    game.mouse_button(1, true, 100, 100);
    game.mouse_button(1, false, 100, 100);
    run(MouseQueue::kClickDelay + 1);
    CHECK(!w.state.selection.active && !game.world_mouse().firing());
    settle(game);

    // Motion: the mouse goes back to (320, 200); 20 pixels to the left at
    // speed 2, out of a combat stance, give a rate of 20 >> 3 = 2: a left
    // arrow, the player turns left.
    // Standing (save 1 ends on a shot, 0x8: a combat stance for 1138:0070).
    do_anim(game.kernel(), w, game.animations(), 1, 0, kDirCurrent);
    game.mouse_move(320, 200);
    run(1);
    settle(game);
    CHECK(!stance_not_moving(w, 1));
    const int dir = w.npc_direction(1) & 0xF;
    game.mouse_move(318, 200);  // rate 2 >> 3 = 0: nothing
    run(1);
    CHECK(game.cursor().x() == 320 && game.cursor().y() == 200);
    settle(game);
    CHECK((w.npc_direction(1) & 0xF) == dir);
    game.mouse_move(300, 200);
    run(1);
    CHECK(game.cursor().x() == 320 && !w.state.mouse_turn.active && w.state.mouse_turn.rate == 2);
    settle(game);
    const int turned = w.npc_direction(1) & 0xF;
    std::printf("mouse: direction %d -> %d\n", dir, turned);
    CHECK(turned == ((dir - 1) & 0xF) || turned == ((dir - 2) & 0xF));
    // To the right: back (the way ahead is clear there).
    game.mouse_move(340, 200);
    run(1);
    settle(game);
    CHECK((w.npc_direction(1) & 0xF) == dir);

    // Right button: after the double-click delay, the player walks forward
    // until the release.
    const std::int32_t x0 = player->x, y0 = player->y;
    game.mouse_button(2, true, 320, 200);
    run(MouseQueue::kClickDelay - 1);
    CHECK(!game.world_mouse().walking());
    run(2);
    CHECK(game.world_mouse().walking());
    run(60);
    std::printf("mouse: walked from (%d, %d) to (%d, %d), last action 0x%X\n", x0, y0, player->x, player->y,
                last_action(game));
    CHECK(player->x != x0 || player->y != y0);
    game.mouse_button(2, false, 320, 200);
    run(1);
    CHECK(!game.world_mouse().walking());
    settle(game);
    CHECK(last_action(game) == 0);
    const std::int32_t x1 = player->x, y1 = player->y;
    run(30);
    CHECK(player->x == x1 && player->y == y1);

    // A double click of the right button draws the weapon (7); in the
    // combat stance, another one holsters it (0xB).
    auto double_click = [&](int button) {
        game.mouse_button(button, true, 320, 200);
        game.mouse_button(button, false, 320, 200);
        run(3);
        game.mouse_button(button, true, 320, 200);
        run(1);
        game.mouse_button(button, false, 320, 200);
        run(1);
    };
    double_click(2);
    settle(game);
    const std::uint16_t drawn = last_action(game);
    std::printf("mouse: after a double click, last action 0x%X\n", drawn);
    CHECK(stance_not_moving(w, 1) && !game.world_mouse().walking());
    // In the combat stance, 40 pixels to the left: rate 40 >> 1 = 20, the
    // crosshair of the aiming mode turns by 10 degrees.
    const int angle = w.state.aim.angle;
    game.mouse_move(280, 200);
    run(1);
    std::printf("mouse: aiming mode %d, angle %d -> %d\n", w.state.aim.active, angle, w.state.aim.angle);
    CHECK(w.state.aim.active && w.state.mouse_turn.rate == 20 && w.state.aim.angle == angle - 10);
    settle(game);

    // Left button, in the combat stance: Space, a shot.
    const int shots = game.shots_fired();
    game.mouse_button(1, true, 320, 200);
    run(MouseQueue::kClickDelay + 1);
    CHECK(game.world_mouse().firing());
    run(60);
    game.mouse_button(1, false, 320, 200);
    run(1);
    CHECK(!game.world_mouse().firing());
    settle(game);
    std::printf("mouse: shots %d -> %d\n", shots, game.shots_fired());
    CHECK(game.shots_fired() > shots);

    double_click(2);
    settle(game);
    std::printf("mouse: after a second double click, last action 0x%X\n", last_action(game));
    CHECK(last_action(game) == 0xB || last_action(game) == 0x10);


    // The menu takes the mouse back.
    press(game, 0x01, false, 0x1B);
    CHECK(game.menu() && !game.mouse_in_play());
    CHECK(game.machine().faults().empty());
}

// Using scenery on SAVEGAME.1: a chest opens and hands over the items of its
// content markers (0x476), a dead body is searched once.
void test_scenery_use(const std::filesystem::path& data) {
    const auto carried = [](World& w, std::uint16_t shape) -> const Item* {
        const Item* found = nullptr;
        w.for_each([&](const Item& i) {
            if (i.parent == 1 && i.shape == shape) found = &i;
        });
        return found;
    };
    // The first entry of the pickup list, and how many are used.
    const auto pickup = [](World& w) {
        int used = 0;
        for (const PickupEntry& e : w.state.pickups.entries()) used += e.name != nullptr;
        const PickupEntry& first = w.state.pickups.entries()[0];
        std::printf("scenery: pickups %d, first \"%s\" x%d (gump %u:%u)\n", used, first.name ? first.name : "",
                    first.count, first.icon_shape, first.icon_frame);
        return std::pair<int, PickupEntry>{used, first};
    };
    const auto use = [](Game& game, ObjId item) {
        use_item(game.world(), item);
        for (int f = 0; f < 120; ++f) game.update(kFrameUs);
    };
    {
        // CHEST_NS 3169, marker 3156 (map 0x3E, npc 0x60): a new 0x33E.
        Game game(data);
        game.load_save(1);
        World& w = game.world();
        CHECK(!carried(w, 0x33E) && w.get(3156));
        use(game, 3169);
        const Item* chest = w.get(3169);
        std::printf("scenery: chest 3169 -> %04X:%u, marker %s, carried 0x33E %d\n", chest->shape, chest->frame,
                    w.get(3156) ? "kept" : "gone", carried(w, 0x33E) != nullptr);
        CHECK(chest->shape == 0x54A && chest->frame == 4 && !w.get(3156) && carried(w, 0x33E));
        const auto [used, first] = pickup(w);
        CHECK(used == 1 && std::string(first.name ? first.name : "") == "CHARGEUR SG-A1" && first.count == 1 &&
              first.icon_shape == 5 && first.icon_frame == 10);
        // Gone after 3 to 4 seconds.
        for (int f = 0; f < 4 * 60; ++f) game.update(kFrameUs);
        CHECK(pickup(w).first == 0);
        CHECK(game.machine().faults().empty() && game.machine().unbound_intrinsic_calls().empty());
    }
    {
        // CHEST_EW 3160, two markers of 0x33D: combined with the ammunition
        // the player carries.
        Game game(data);
        game.load_save(1);
        World& w = game.world();
        const std::uint16_t ammo = carried(w, 0x33D)->quality;
        use(game, 3160);
        std::printf("scenery: chest 3160 -> %04X:%u, ammunition 0x33D %u -> %u\n", w.get(3160)->shape,
                    w.get(3160)->frame, ammo, carried(w, 0x33D)->quality);
        CHECK(w.get(3160)->shape == 0x54B && !w.get(3158) && !w.get(3159) && carried(w, 0x33D)->quality == ammo + 2);
        // Both in one entry.
        const auto [used, first] = pickup(w);
        CHECK(used == 1 && std::string(first.name ? first.name : "") == "CHARGEUR BA-41" && first.count == 2);
        // Opened: using it again does nothing.
        use(game, 3160);
        CHECK(carried(w, 0x33D)->quality == ammo + 2);
    }
    {
        // D_MAINT 8389 holds credits (0x4ED 8388): searched once.
        Game game(data);
        game.load_save(1);
        World& w = game.world();
        const std::uint16_t credits = carried(w, 0x4ED)->quality;
        use(game, 8389);
        const std::uint16_t after = carried(w, 0x4ED)->quality;
        std::printf("scenery: dead body 8389: credits %u -> %u, status %04X\n", credits, after, w.get(8389)->flags);
        CHECK(!w.get(8388) && after > credits && (w.get(8389)->flags & 0x1000));
        // Credits count their quality.
        const auto [used, first] = pickup(w);
        CHECK(used == 1 && std::string(first.name ? first.name : "") == "CREDITS" && first.count == after - credits);
        CHECK(game.machine().unbound_intrinsic_calls().empty());
    }
}

// The computers: ComputerGump::I_readComputer (0x0FE) from a monitor's script.
void test_computer(const std::filesystem::path& data) {
    // 13A0:03C8: a space becomes a break at a character with 20 or more
    // counted since the last one.
    CHECK(ComputerGump::wrap("abcdefghij klmnopqrstu vw") == "abcdefghij^klmnopqrstu vw");
    CHECK(ComputerGump::wrap("abc^defghijklmnopqrstuvw x") == "abc^defghijklmnopqrstuvw^x");
    CHECK(ComputerGump::wrap("abcdefghijklmnopqrstuvwxyz") == "abcdefghijklmnopqrstuvwxyz");
    // MONITEW 5449 (save 2): a message through TEXTFILE, its code from
    // numToStr (0x0FF).
    Game game(data);
    game.load_save(2);
    World& w = game.world();
    use_item(w, 5449);
    for (int f = 0; f < 3; ++f) game.update(kFrameUs);
    const auto* computer = dynamic_cast<const ComputerGump*>(game.screen());
    CHECK(computer != nullptr);
    if (!computer) return;
    std::printf("computer: \"%s\"\n", computer->text().c_str());
    CHECK(computer->text().rfind("De : DBEYER^", 0) == 0 &&
          computer->text().find("^962 est le code^") != std::string::npos);
    CHECK(!w.state.status_bar_visible && !game.cursor().visible());
    // One character per tick.
    const int shown = computer->shown();
    for (int f = 0; f < 10; ++f) game.update(kFrameUs);
    std::printf("computer: %d characters shown, then %d\n", shown, computer->shown());
    CHECK(computer->shown() > shown && computer->shown() <= shown + 10);
    // Escape shows the rest of the page; then a key per page, and Escape or
    // Enter closes once the text has ended.
    const auto escape = [&game] {
        game.key(0x01, false, true, 0, 0x1B);
        game.key(0x01, false, false, 0, 0x1B);
        game.update(kFrameUs);
    };
    int pages = 0;
    for (int i = 0; i < 20 && game.screen(); ++i) {
        escape();
        if (const auto* c = dynamic_cast<const ComputerGump*>(game.screen())) {
            CHECK(c->page_full() || c->ended());
            pages += c->page_full();
            if (c->ended()) {
                escape();
                break;
            }
            escape();  // the next page
        }
    }
    std::printf("computer: %d full pages, closed %d\n", pages, game.screen() == nullptr);
    CHECK(!game.screen() && w.state.status_bar_visible);
    for (int f = 0; f < 60; ++f) game.update(kFrameUs);
    CHECK(game.machine().faults().empty() && game.machine().unbound_intrinsic_calls().empty());
}

void test_keypad(const std::filesystem::path& data) {
    // KEYPAD 5459 (save 2): the code of its VALBOX, 962, the one given by
    // monitor 5449.
    constexpr ObjId kKeypad = 5459;
    const auto open = [](Game& game) -> const KeypadGump* {
        use_item(game.world(), kKeypad);
        for (int f = 0; f < 3; ++f) game.update(kFrameUs);
        return dynamic_cast<const KeypadGump*>(game.screen());
    };
    const auto press = [](Game& game, std::uint8_t scancode, std::uint8_t ascii) {
        game.key(scancode, false, true, 0, ascii);
        game.key(scancode, false, false, 0, ascii);
        game.update(kFrameUs);
    };
    const auto flags = [](Game& game) { return game.world().get(kKeypad)->flags; };
    const auto clean = [](Game& game) {
        return game.machine().faults().empty() && game.machine().unbound_intrinsic_calls().empty();
    };

    // Escape: -1, the script only clears its busy flag (0x1000).
    {
        Game game(data);
        game.load_save(2);
        const KeypadGump* keypad = open(game);
        CHECK(keypad != nullptr);
        if (!keypad) return;
        std::printf("keypad: code %d at (%d, %d)\n", keypad->code(), keypad->x(), keypad->y());
        CHECK(keypad->code() == 962 && game.cursor().visible() && !game.world().state.status_bar_visible);
        CHECK((flags(game) & 0x1000) != 0);
        press(game, 0x0A, '9');
        press(game, 0x02, '!');  // shifted 1
        CHECK(keypad->digit_count() == 2);
        press(game, 0x0E, 0x08);
        CHECK(keypad->digit_count() == 1);
        press(game, 0x01, 0x1B);
        for (int f = 0; f < 30; ++f) game.update(kFrameUs);
        std::printf("keypad: escape, closed %d, flags %04X\n", game.screen() == nullptr, flags(game));
        CHECK(!game.screen() && (flags(game) & 0x1004) == 0 && game.world().state.status_bar_visible);
        CHECK(clean(game));
    }
    // The buttons: 1 then enter, the wrong code.
    {
        Game game(data);
        game.load_save(2);
        const KeypadGump* keypad = open(game);
        CHECK(keypad != nullptr);
        if (!keypad) return;
        const auto click = [&game, keypad](int button) {
            const KeypadGump::Rect& r = KeypadGump::kButtonRects[static_cast<std::size_t>(button)];
            const int x = keypad->x() + (r.x0 + r.x1) / 2;
            const int y = keypad->y() + (r.y0 + r.y1) / 2;
            game.mouse_button(true, x, y);
            game.update(kFrameUs);
            CHECK(game.screen() == keypad && keypad->pressed(button));
            game.mouse_button(false, x, y);
            game.update(kFrameUs);
        };
        click(0);
        CHECK(keypad->digit_count() == 1 && !keypad->pressed(0));
        CHECK(game.audio() && game.audio()->voices.handle_playing(KeypadGump::kSoundButton) != SoundManager::kNoHandle);
        click(KeypadGump::kEnter);
        CHECK(!game.screen());
        CHECK(game.audio() && game.audio()->voices.handle_playing(KeypadGump::kSoundWrong) != SoundManager::kNoHandle);
        for (int f = 0; f < 120; ++f) game.update(kFrameUs);
        std::printf("keypad: wrong code, flags %04X\n", flags(game));
        CHECK((flags(game) & 0x1004) == 0);
        CHECK(clean(game));
    }
    // The keys: 962 then Enter, the door's code.
    {
        Game game(data);
        game.load_save(2);
        CHECK(open(game) != nullptr);
        press(game, 0x0A, '9');
        press(game, 0x07, '6');
        press(game, 0x03, '2');
        press(game, 0x1C, 0x0D);
        CHECK(!game.screen());
        CHECK(game.audio() && game.audio()->voices.handle_playing(KeypadGump::kSoundRight) != SoundManager::kNoHandle);
        for (int f = 0; f < 120; ++f) game.update(kFrameUs);
        std::printf("keypad: right code, flags %04X\n", flags(game));
        CHECK((flags(game) & 0x1000) == 0 && (flags(game) & 0x0004) != 0);
        CHECK(clean(game));
    }
}

// SURCAMNS 3224 (save 7, quality low 0xFE): the view of its EVENT egg, in
// grey, and the control of the NPC near it whose egg quality (record +0x63)
// is 0xFE, robot 10; Escape gives the control back. Without such an NPC, the
// view stays until a key is held (0x096).
void test_monitor(const std::filesystem::path& data) {
    constexpr ObjId kMonitor = 3224;
    constexpr ObjId kRobot = 10;
    const auto show = [](Game& game, const char* when) {
        const World& w = game.world();
        std::printf("monitor: %s, camera (%d, %d, %d), controlled %u, stasis %d, status bar %d\n", when,
                    w.state.camera_x, w.state.camera_y, w.state.camera_z, w.state.controlled_npc,
                    w.state.avatar_in_stasis, w.state.status_bar_visible);
    };
    const auto run = [](Game& game, int frames) {
        for (int f = 0; f < frames; ++f) game.update(kFrameUs);
    };
    // Back to the player: camera on him, status bar, no stasis.
    const auto restored = [](Game& game) {
        const World& w = game.world();
        const Item* p = w.get(1);
        return w.state.controlled_npc == 1 && !w.state.avatar_in_stasis && w.state.status_bar_visible &&
               w.state.camera_x == p->x && w.state.camera_y == p->y && !game.menu() && !game.screen();
    };
    const auto clean = [](Game& game) {
        return game.machine().faults().empty() && game.machine().unbound_intrinsic_calls().empty();
    };

    // The robot.
    {
        Game game(data);
        game.load_save(7);
        World& w = game.world();
        CHECK(w.npc_record(kRobot)[npc::kEggQuality] == 0xFE);
        use_item(w, kMonitor);
        run(game, 200);
        show(game, "robot");
        CHECK(w.state.controlled_npc == kRobot && w.state.avatar_in_stasis && !w.state.status_bar_visible);
        // The arrows drive the robot.
        const World::Point from = w.location(*w.get(kRobot));
        game.key(0x48, true, true, 0, 0);
        run(game, 60);
        game.key(0x48, true, false, 0, 0);
        run(game, 20);
        const World::Point to = w.location(*w.get(kRobot));
        std::printf("monitor: robot (%d, %d) -> (%d, %d)\n", from.x, from.y, to.x, to.y);
        CHECK(to.x != from.x || to.y != from.y);
        game.key(0x01, false, true, 0, 0x1B);
        game.key(0x01, false, false, 0, 0x1B);
        run(game, 120);
        show(game, "escape");
        CHECK(restored(game) && clean(game));
    }
    // No NPC to control: a key held ends the view.
    {
        Game game(data);
        game.load_save(7);
        World& w = game.world();
        w.npc_record(kRobot)[npc::kEggQuality] = 0;
        const std::int32_t player_x = w.get(1)->x;
        use_item(w, kMonitor);
        run(game, 300);
        show(game, "view");
        CHECK(w.state.controlled_npc == 1 && w.state.avatar_in_stasis && !w.state.status_bar_visible);
        CHECK(w.state.camera_x != player_x);
        game.key(0x48, true, true, 0, 0);
        run(game, 5);
        game.key(0x48, true, false, 0, 0);
        run(game, 120);
        show(game, "key");
        CHECK(restored(game) && clean(game));
    }
}

// CAMERACO 6082 (save 2), once on screen, sweeps frames 0..4 and back; when
// it faces the controlled NPC and sees it (Item::I_getRangeIfVisible 0x044),
// it stops, beeps (0xE6), takes the status 0x1000 and raises the alarm.
void test_security_camera(const std::filesystem::path& data) {
    constexpr ObjId kCamera = 6082;
    for (const bool in_view : {false, true}) {
        Game game(data);
        game.load_save(2);
        World& w = game.world();
        const Item* cam = w.get(kCamera);
        // Behind a wall, then in front of it.
        if (in_view) w.move_item(1, cam->x + 256, cam->y + 256, 0);
        else w.move_item(1, cam->x - 64, cam->y + 400, 0);
        const int range = range_if_visible(w, kCamera, 1);
        std::set<int> frames;
        bool beep = false;
        for (int f = 0; f < 600; ++f) {
            game.update(kFrameUs);
            frames.insert(cam->frame);
            beep = beep || game.audio()->sources.is_playing(0xE6, kCamera);
        }
        std::printf("camera: in view %d, range %d, frames %zu, beep %d, status %04X, alarm %d\n", in_view, range,
                    frames.size(), beep, cam->flags & 0x1000, w.state.alert_active);
        if (in_view) CHECK(range == 16 && beep && (cam->flags & 0x1000) && w.state.alert_active);
        else CHECK(range == 0 && frames.size() == 5 && !beep && !(cam->flags & 0x1000) && !w.state.alert_active);
        CHECK(game.machine().faults().empty() && game.machine().unbound_intrinsic_calls().empty());
    }
}
void test_stations(const std::filesystem::path& data) {
    Game game(data);
    game.load_save(1);
    World& w = game.world();
    Audio* audio = game.audio();
    CHECK(audio != nullptr);
    if (!audio) return;
    auto set_word = [&](std::size_t at, int v) {
        auto r = w.npc_record(1);
        r[at] = static_cast<std::uint8_t>(v);
        r[at + 1] = static_cast<std::uint8_t>(v >> 8);
    };
    set_word(0, 100);
    start_healer(game.kernel(), w);
    CHECK(w.state.avatar_in_stasis && audio->sources.is_playing(0xDB, 1));
    game.update(kFrameUs);
    CHECK(audio->sources.is_playing(0xBA, 1));
    for (int i = 0; i < 80; ++i) game.update(kFrameUs);
    std::printf("stations: hit points 100 -> %u, healing sound %d\n", w.npc_hp(1), audio->sources.is_playing(0xDB, 1));
    CHECK(w.npc_hp(1) == 150 && !w.state.avatar_in_stasis && !audio->sources.is_playing(0xDB, 1));

    std::printf("stations: battery %u, max energy %d\n", w.state.battery, max_energy(w, 1));
    CHECK(w.state.battery == 1 && max_energy(w, 1) == 2500);
    set_word(0x57, 0);
    start_battery_charger(game.kernel(), w);
    for (int i = 0; i < 120; ++i) game.update(kFrameUs);
    CHECK(w.npc_mana(1) == 2500 && !audio->sources.is_playing(0xA4, 1));
}

// The player dropped from 48 units lands hard (1040:04BC): action 0x3B and
// a grunt.
void test_hard_landing(const std::filesystem::path& data) {
    Game game(data);
    game.load_save(13);
    World& w = game.world();
    settle(game);
    const Item* player = w.get(1);
    const int ground = player->z & 0xFF;
    w.move_item(1, player->x, player->y, ground + 48);
    fall(game.kernel(), w, 1);
    bool stumbled = false, grunt = false;
    for (int i = 0; i < 200; ++i) {
        game.update(kFrameUs);
        stumbled = stumbled || last_action(game) == 0x3B;
        grunt = grunt || game.audio()->sources.is_playing(0x8F, 1);
    }
    std::printf("landing: z %d -> %d, stumbled %d, grunt %d\n", ground + 48, w.get(1)->z & 0xFF, stumbled, grunt);
    CHECK((w.get(1)->z & 0xFF) == ground && stumbled && grunt);
}

// Milestone 4: a pistol shot (fire type 11) at an NPC placed in front of the player.
std::size_t projectiles(Game& game) { return game.kernel().count_processes(0, ProjectileProcess::kType); }

// Fires as the shot frame of `shooter` facing `dir` would (action 8): from its
// muzzle offset, outside the shooter's box.
Pid fire_from_muzzle(Game& game, ObjId shooter, int dir, int fire_type, bool aim) {
    World& w = game.world();
    // A real shot leaves in the shooter's direction (the crosshair of the
    // aiming mode is placed from it).
    if (auto r = w.npc_record(shooter); !r.empty()) r[npc::kDirection] = static_cast<std::uint8_t>(dir);
    World::Point m{0, 0, 24};
    if (const AnimAction* a = game.animations().action(w.get(shooter)->shape, 8)) {
        for (std::size_t i = 0; i < a->frame_count(); ++i) {
            const AnimFrame* f = a->frame(dir, i);
            if (f && f->fires_weapon()) {
                m = {f->muzzle_a * 16, f->muzzle_b * 16, f->muzzle_z * 8};
                break;
            }
        }
    }
    return fire_weapon(game.kernel(), w, game.animations(), shooter, m.x, m.y, m.z, dir, fire_type, aim);
}

void test_shot_kills(const std::filesystem::path& data) {
    Game game(data);
    game.load_save(13);
    World& w = game.world();
    const Item* player = w.get(1);
    ObjId victim = 0;
    for (ObjId id = 2; id < 256 && !victim; ++id) {
        const Item* it = w.get(id);
        if (it && !w.is_dead(id) && (it->z & 0xFF) != 254 && w.npc_hp(id) > 0 && game.animations().action(it->shape, 0x12))
            victim = id;
    }
    CHECK(victim != 0);
    if (!victim) return;
    const int dir = w.npc_direction(1) & 0xE;
    w.move_item(victim, player->x + kDirectionDx[dir] * 96, player->y + kDirectionDy[dir] * 96, player->z);
    const int hp = w.npc_hp(victim);
    std::size_t sprites_before = 0;
    w.for_each([&](const Item& i) { sprites_before += i.shape == kProjectileShape; });

    fire_from_muzzle(game, 1, dir, 11, false);
    CHECK(projectiles(game) == 1);
    int frames = 0;
    for (; frames < 300 && projectiles(game) != 0; ++frames) game.update(kFrameUs);
    for (int i = 0; i < 10; ++i) game.update(kFrameUs);  // the death animation starts
    const auto r = w.npc_record(victim);
    const std::uint16_t last = static_cast<std::uint16_t>(r[npc::kLastAnim] | r[npc::kLastAnim + 1] << 8);
    std::printf("shot: victim %u (shape %u) hp %d -> %u, dead %d, last action 0x%X, %d frames\n", victim,
                w.get(victim)->shape, hp, w.npc_hp(victim), w.is_dead(victim), last, frames);
    CHECK(projectiles(game) == 0);
    CHECK(w.npc_hp(victim) < hp);
    // 14..20 damage, x5 at difficulty 1: at least 70.
    CHECK(hp > 70 || (w.is_dead(victim) && w.npc_hp(victim) == 0 && (last == 0x12 || last == 0x14)));
    // A human's death cry (or a cry of pain if it lives), at the victim.
    const std::uint16_t shape = w.get(victim)->shape;
    const bool robot = shape == 0x4C8 || shape == 0x338 || shape == 0x45D || shape == 0x2CB || shape == 0x4E6 ||
                       shape == 0x383 || shape == 0x385;
    bool cry = false;
    for (int sfx : {0x88, 0x8C, 0x8F, 0xD8, 0x10}) cry = cry || game.audio()->sources.is_playing(sfx, victim);
    std::printf("shot: victim's cry %d\n", cry);
    CHECK(robot || cry);
    std::size_t sprites_after = 0;
    w.for_each([&](const Item& i) { sprites_after += i.shape == kProjectileShape; });
    std::printf("shot: items of shape 0x1D9 before %zu, after %zu\n", sprites_before, sprites_after);
    CHECK(sprites_after == sprites_before);
    CHECK(game.machine().faults().empty());
}

// Milestone 4: aiming (TRACKER_1128_0158). An NPC placed 8.9 degrees off
// the direction of the shot is found and aimed at; a straight shot would pass
// beside it.
void test_aiming(const std::filesystem::path& data) {
    Game game(data);
    game.load_save(13);
    for (int i = 0; i < 5; ++i) game.update(kFrameUs);
    World& w = game.world();

    // The tracked list: items of the fast area, not broken, trackable shapes.
    std::size_t listed = 0;
    bool consistent = true;
    for (ObjId id : w.tracked()) {
        if (id == 0) continue;
        ++listed;
        const Item* it = w.get(id);
        consistent = consistent && it && (it->flags & kItemFastArea) && !(it->flags & kItemBroken) &&
                     w.is_trackable(it->shape);
    }
    std::printf("aim: %zu tracked items\n", listed);
    CHECK(listed == w.tracked_count() && listed > 0 && consistent);

    const Item* player = w.get(1);
    ObjId victim = 0;
    for (ObjId id = 2; id < 256 && !victim; ++id) {
        const Item* it = w.get(id);
        if (it && !w.is_dead(id) && (it->z & 0xFF) != 254 && w.npc_hp(id) > 0 && w.is_trackable(it->shape))
            victim = id;
    }
    CHECK(victim != 0);
    if (!victim) return;
    // Every other tracked item out of the way: they would compete for the shot.
    std::vector<ObjId> others;
    for (ObjId id : w.tracked())
        if (id != 0 && id != victim) others.push_back(id);
    for (ObjId id : others) w.set_broken(*w.get(id));
    const int dir = 4;  // +x
    w.move_item(victim, player->x + 256, player->y - 40, player->z);
    CHECK(find_target(w, 1, dir, true) == victim);
    CHECK(find_target(w, 1, (dir + 1) & 0xF, true) == 0);
    const World::Point aim = aim_point(w, 1, victim);
    const Item* v = w.get(victim);
    std::printf("aim: victim %u at (%d, %d, %d), aim point (%d, %d, %d)\n", victim, v->x, v->y, v->z & 0xFF, aim.x,
                aim.y, aim.z);
    CHECK(aim.x == v->x - 24 && aim.y == v->y - 24 && aim.z == ((v->z & 0xFF) + 64));

    // Without aiming the shot goes straight ahead and misses.
    const int hp = w.npc_hp(victim);
    fire_from_muzzle(game, 1, dir, 11, false);
    for (int frames = 0; frames < 300 && projectiles(game) != 0; ++frames) game.update(kFrameUs);
    CHECK(w.npc_hp(victim) == hp);

    // In the game the last action is the shot (action 8, 16 directions).
    w.npc_record(1)[npc::kLastAnim] = 8;
    w.npc_record(1)[npc::kLastAnim + 1] = 0;
    const World::Point aim_now = aim_point(w, 1, victim);
    std::printf("aim: victim now at (%d, %d, %d), aim point (%d, %d, %d)\n", v->x, v->y, v->z & 0xFF, aim_now.x,
                aim_now.y, aim_now.z);
    const Pid pid = fire_from_muzzle(game, 1, dir, 11, true);
    const auto* p = dynamic_cast<const ProjectileProcess*>(game.kernel().get(pid));
    CHECK(p != nullptr);
    if (!p) return;
    // Pistol: spread = distance / 10 around the aim point.
    const int spread = std::max(std::abs(aim_now.x - p->position().x), std::abs(aim_now.y - p->position().y)) / 10;
    std::printf("aim: projectile towards (%d, %d, %d), spread %d\n", p->target().x, p->target().y, p->target().z, spread);
    CHECK(std::abs(p->target().x - aim_now.x) <= spread && std::abs(p->target().y - aim_now.y) <= spread);
    for (int frames = 0; frames < 300 && projectiles(game) != 0; ++frames) game.update(kFrameUs);
    std::printf("aim: victim hp %d -> %u\n", hp, w.npc_hp(victim));
    CHECK(w.npc_hp(victim) < hp);
    CHECK(game.machine().faults().empty());
}

// Milestone 6: the aiming mode and target designation (aim_mode.hpp), on
// SAVEGAME.13.
void test_aim_mode(const std::filesystem::path& data) {
    Game game(data);
    game.load_save(13);
    auto run = [&](int frames) {
        for (int i = 0; i < frames; ++i) game.update(kFrameUs);
    };
    run(5);
    World& w = game.world();
    const GameState::Aim& aim = w.state.aim;
    const Item* player = w.get(1);
    // One NPC that can be marked; the other tracked items out of the way.
    ObjId victim = 0;
    for (ObjId id = 2; id < 256 && !victim; ++id) {
        const Item* it = w.get(id);
        const ShapeInfo* info = it ? w.shape_info(it->shape) : nullptr;
        if (it && !w.is_dead(id) && (it->z & 0xFF) != 254 && w.is_trackable(it->shape) && info &&
            info->is(ShapeInfo::kOccludes))
            victim = id;
    }
    CHECK(victim != 0);
    if (!victim) return;
    std::vector<ObjId> others;
    for (ObjId id : w.tracked())
        if (id != 0 && id != victim) others.push_back(id);
    for (ObjId id : others) w.set_broken(*w.get(id));
    for (Pid pid = 1; pid < 400; ++pid)
        if (Process* p = game.kernel().get(pid); p && p->owner() == victim) p->fail();  // keep it still
    w.move_item(victim, player->x, player->y + 600, player->z);  // behind the player

    // 1. The combat stance enters the mode: the crosshair, 200 units ahead.
    w.npc_record(1)[npc::kDirection] = 4;
    do_anim(game.kernel(), w, game.animations(), 1, 4, kDirCurrent);
    const ShapeInfo* pinfo = w.shape_info(player->shape);
    const int cx = player->x - pinfo->footpad_x * 8, cy = player->y - pinfo->footpad_y * 8;
    const ScreenSprite* cross = w.state.screen_sprites.get(aim.crosshair);
    CHECK(aim.active && cross && cross->shape == kCrosshairShape && aim.angle == 3);
    if (!cross) return;
    const int ex = cx + static_cast<int>((std::int64_t{w.trig().cos_deg(3)} * 200) >> 16);
    const int ey = cy + static_cast<int>((std::int64_t{w.trig().sin_deg(3)} * 200) >> 16);
    std::printf("aim mode: crosshair at (%d, %d, %d), player centre (%d, %d)\n", cross->x, cross->y, cross->z, cx, cy);
    CHECK(cross->x == ex && cross->y == ey && cross->x == cx + 199 && cross->y == cy + 9);  // entry 2: 2.8 degrees
    run(30);

    // 2. The right arrow turns the crosshair by 2 degrees (no Shift); the
    // player turns to face it (docs/bugs.md: "impossible de tourner").
    constexpr std::uint8_t kRight = 0x4D;
    for (int i = 0; i < 12; ++i) {
        game.key(kRight, true, true, 0);
        run(2);
        game.key(kRight, true, false, 0);
        run(2);
    }
    run(30);
    cross = w.state.screen_sprites.get(aim.crosshair);
    CHECK(aim.active && cross && aim.angle == 27);
    if (!cross) return;
    const int facing = direction_16(cx, cy, cross->x, cross->y);
    std::printf("aim mode: angle %d, crosshair (%d, %d), player direction %d (crosshair's %d)\n", aim.angle, cross->x,
                cross->y, w.npc_direction(1), facing);
    CHECK(facing == 5 && w.npc_direction(1) == 5);

    // 3. A target in that direction is marked within a second (DumbTimer).
    const int dir = 5;
    w.move_item(victim, player->x + kDirectionDx[dir] * 128, player->y + kDirectionDy[dir] * 128, player->z);
    CHECK(find_target(w, 1, dir, true) == victim);
    run(70);
    const Item* v = w.get(victim);
    const ScreenSprite* marker = w.state.screen_sprites.get(aim.marker);
    std::printf("aim mode: target %u at (%d, %d, %d), marker %s (%d, %d, %d) frame %d\n", aim.target, v->x, v->y,
                v->z & 0xFF, marker ? "at" : "missing", marker ? marker->x : 0, marker ? marker->y : 0,
                marker ? marker->z : 0, marker ? marker->frame : 0);
    CHECK(aim.target == victim && marker && marker->shape == SpriteProcess::kTargetMarker);
    if (!marker) return;
    // NPC footprint 3 x 3 x 10: height 10 / 2 + 1 = 6, square, so (x, y, z + 48).
    CHECK(marker->x == v->x && marker->y == v->y && marker->z == (v->z & 0xFF) + 48);
    run(70);  // frames 0..5, 10 ticks each, then it stays on frame 5
    CHECK(marker->frame == 5 && aim.marker_pid == 0 && w.state.screen_sprites.get(aim.marker) == marker);

    // 4. The marker follows its target.
    w.move_item(victim, v->x + 16, v->y + 8, v->z);
    CHECK(aim.target == victim && marker->x == v->x && marker->y == v->y);

    // 5. Destroyed, it is no longer marked.
    const int slot = aim.marker;
    w.destroy(victim);
    CHECK(aim.target == 0 && aim.marker == -1 && w.state.screen_sprites.get(slot) == nullptr);

    // 6. No target: the shot flies towards the crosshair.
    const Pid pid = fire_from_muzzle(game, 1, dir, 11, true);
    const auto* p = dynamic_cast<const ProjectileProcess*>(game.kernel().get(pid));
    cross = w.state.screen_sprites.get(aim.crosshair);
    CHECK(p != nullptr && cross != nullptr);
    if (!p || !cross) return;
    std::printf("aim mode: shot towards (%d, %d, %d), crosshair (%d, %d)\n", p->target().x, p->target().y,
                p->target().z, cross->x, cross->y);
    CHECK(std::abs(p->target().x - cross->x) <= 20 && std::abs(p->target().y - cross->y) <= 20);
    run(60);

    // 7. Walking leaves the mode: no crosshair.
    do_anim(game.kernel(), w, game.animations(), 1, 1, kDirCurrent);
    bool crosshair_left = false;
    for (const ScreenSprite& s : w.state.screen_sprites.all()) crosshair_left |= s.shape == kCrosshairShape;
    CHECK(!aim.active && aim.crosshair == -1 && !crosshair_left);
    CHECK(game.machine().faults().empty());
}

// Milestone 4: enemies fight back (ATTACK.C), on SAVEGAME.13.
std::vector<ObjId> attackers(Game& game) {
    std::vector<ObjId> ids;
    game.kernel().for_each_process([&](const Process& p) {
        if (p.type() == AttackProcess::kType) ids.push_back(p.owner());
    });
    return ids;
}

void test_enemies(const std::filesystem::path& data) {
    // 1. NPC 2 guards (activity 9: in combat, weapon drawn, stays put). Put
    // 80 direction steps in front of the player, it enters the fast area, gets its
    // attack process, turns to the player and fires.
    {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        const Item* player = w.get(1);
        const int dir = w.npc_direction(1) & 0xE;
        w.move_item(2, player->x + kDirectionDx[dir] * 80, player->y + kDirectionDy[dir] * 80, player->z);
        const auto ids = attackers(game);
        CHECK(ids.size() == 1 && ids[0] == 2 && in_combat(w, 2) && w.npc_word(2, npc::kTarget) == 1);
        const int hp = w.npc_hp(1);
        const int shots = game.shots_fired();
        int first_shot = -1;
        for (int f = 0; f < 600; ++f) {
            game.update(kFrameUs);
            if (first_shot < 0 && game.shots_fired() > shots) first_shot = f;
        }
        const int facing = w.npc_direction(2);
        const Item* guard = w.get(2);
        const int towards = closest_direction_in_range(guard->x, guard->y, player->x, player->y, 16, 0, 15);
        std::printf("enemies: guard faces %d (player at %d), first shot after %d frames, %d shots in 10 s, "
                    "player hp %d -> %u\n", facing, towards, first_shot, game.shots_fired() - shots, hp, w.npc_hp(1));
        CHECK(facing == towards);
        CHECK(first_shot >= 0 && game.shots_fired() - shots >= 3);
        CHECK(w.npc_hp(1) < hp);
        CHECK(game.machine().faults().empty());
    }
    // 2. NPC 6 stands (activity 0, default activities 1, 5, 5). Hit by the
    // player, it takes its default activity 2 (5: combat) and targets the
    // player.
    {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        w.state.difficulty = 0;  // no x5: the NPC survives the hit
        const Item* player = w.get(1);
        const int dir = w.npc_direction(1) & 0xE;
        w.move_item(6, player->x + kDirectionDx[dir] * 64, player->y + kDirectionDy[dir] * 64, player->z);
        CHECK(attackers(game).empty() && !in_combat(w, 6));
        apply_damage(game.kernel(), w, game.animations(), 6, 1, dir, 10, 11);
        const auto ids = attackers(game);
        std::printf("enemies: hit NPC 6: activity %u, in combat %d, target %u, hp %u\n", w.npc_word(6, npc::kActivity),
                    in_combat(w, 6), w.npc_word(6, npc::kTarget), w.npc_hp(6));
        CHECK(ids.size() == 1 && ids[0] == 6);
        CHECK(w.npc_word(6, npc::kActivity) == 5 && in_combat(w, 6) && w.npc_word(6, npc::kTarget) == 1);
        for (int f = 0; f < 120; ++f) game.update(kFrameUs);
        CHECK(game.machine().faults().empty());
    }
    // 4. NPC 16 (activity 5, armed) put 750 units east of the player, off
    // screen: it walks towards the player (path finder), stops when it can
    // shoot, and fires.
    {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        const Item* player = w.get(1);
        w.move_item(16, player->x + 750, player->y, player->z);
        const Item* npc = w.get(16);
        const int shots = game.shots_fired();
        const int start = std::abs(npc->x - player->x);
        bool walked = false;
        int first_shot = -1;
        for (int f = 0; f < 8 * 60; ++f) {
            game.update(kFrameUs);
            walked = walked || game.kernel().count_processes(16, PathfinderProcess::kType) != 0;
            if (first_shot < 0 && game.shots_fired() > shots) first_shot = f;
        }
        const int end = std::abs(npc->x - player->x);
        std::printf("enemies: walker from %d to %d units, first shot after %d frames, %d shots, player hp %u\n",
                    start, end, first_shot, game.shots_fired() - shots, w.npc_hp(1));
        CHECK(walked && end < start - 64);
        CHECK(first_shot > 0 && w.npc_hp(1) < 141);
        CHECK(game.machine().faults().empty());
    }
    // 5. Items (DAMAGE.FLX): barrel 2370 (shape 0x1FD, 50 hit points) loses
    // 20, then breaks: it explodes and is replaced by shape 0x568 frame 1.
    // Item 2427 (0x2A5, 10 hit points) explodes with splash damage and shows
    // its next frame.
    {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        w.state.difficulty = 0;  // damage as given
        Item* barrel = w.get(2370);
        CHECK(barrel && barrel->shape == 0x1FD && barrel->hit_points == 50);
        if (!barrel) return;
        const World::Point at{barrel->x, barrel->y, barrel->z};
        apply_damage(game.kernel(), w, game.animations(), 2370, 1, 0, 20, 11);
        CHECK(w.get(2370) && w.get(2370)->hit_points == 30);
        apply_damage(game.kernel(), w, game.animations(), 2370, 1, 0, 40, 11);
        ObjId replacement = 0;
        for (ObjId id : w.rect_search(at.x, at.y, at.x, at.y))
            if (w.get(id)->shape == 0x568) replacement = id;
        std::printf("items: barrel replaced %d, replacement %u frame %u\n", replacement != 0, replacement,
                    replacement ? w.get(replacement)->frame : 0);
        // (The replacement may reuse the freed id 2370.)
        CHECK(replacement != 0 && w.get(replacement)->frame == 1 && (w.get(replacement)->flags & 0x0100) == 0);

        Item* box = w.get(2427);
        CHECK(box && box->shape == 0x2A5 && box->hit_points == 10);
        if (!box) return;
        std::vector<std::pair<ObjId, std::uint8_t>> around;
        for (ObjId id : w.rect_search(box->x - 80, box->y - 80, box->x + 80, box->y + 80))
            if (id != 2427 && !w.damage_record(w.get(id)->shape).empty()) around.emplace_back(id, w.get(id)->hit_points);
        const std::uint16_t frame = box->frame;
        apply_damage(game.kernel(), w, game.animations(), 2427, 1, 0, 20, 11);
        int hurt = 0;
        for (auto [id, hp] : around)
            if (!w.get(id) || w.get(id)->hit_points != hp || (w.get(id)->flags & 0x100)) ++hurt;
        std::printf("items: box frame %u -> %u, flags 0x%04X, %d of %zu items around damaged\n", frame, box->frame,
                    box->flags, hurt, around.size());
        CHECK(box->frame == frame + 1 && (box->flags & 0x0140) == 0x0140);
        // Explosion sprites: running, then gone with their items.
        const std::size_t sprites = game.kernel().count_processes(0, SpriteProcess::kType);
        std::size_t sprite_items = 0;
        w.for_each([&](const Item& i) { sprite_items += i.sprite_only; });
        for (int f = 0; f < 180; ++f) game.update(kFrameUs);
        std::size_t left = 0;
        w.for_each([&](const Item& i) { left += i.sprite_only; });
        std::printf("items: %zu sprites (%zu items), %zu sprite items left after 3 s\n", sprites, sprite_items, left);
        CHECK(sprites >= 2 && sprite_items >= 2 && game.kernel().count_processes(0, SpriteProcess::kType) == 0 &&
              left == 0);
        CHECK(game.machine().faults().empty());
    }
    // 6. Weapon overlay: the player of SAVEGAME.13 (in combat, shotgun, last
    // action 0x19) is drawn with a piece of shape 0x33B (class 4).
    {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        const WeaponOverlay overlay = WeaponOverlay::load(data / "STATIC/WPNOVLAY.DAT");
        const auto r = w.npc_record(1);
        const std::uint16_t last = static_cast<std::uint16_t>(r[npc::kLastAnim] | r[npc::kLastAnim + 1] << 8);
        const Item* weapon = w.get(w.active_weapon(1));
        const AnimAction* header = game.animations().action(1, last);
        CHECK(weapon && header && in_combat(w, 1));
        if (weapon && header) {
            const auto piece = overlay.piece(weapon->shape, last, *header, w.npc_direction(1), r[npc::kAnimFrame]);
            std::printf("overlay: weapon %u action 0x%X dir %u frame %u -> shape 0x%X frame %u offset (%d, %d)\n",
                        weapon->shape, last, w.npc_direction(1), r[npc::kAnimFrame], piece ? piece->shape : 0,
                        piece ? piece->frame : 0, piece ? piece->dx : 0, piece ? piece->dy : 0);
            CHECK(piece && piece->shape == 0x33B);
        }
    }
    // 7. Guided missile (fire type 9) shot by NPC 2 two directions off: it
    // turns towards the player (its target) and hits.
    {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        const Item* player = w.get(1);
        w.move_item(2, player->x + 160, player->y, player->z);  // east of the player
        for (Pid pid = 1; pid < 200; ++pid)
            if (Process* p = game.kernel().get(pid); p && p->owner() == 2) p->fail();  // keep NPC 2 still
        const int hp = w.npc_hp(1);
        const int west = 12, off = (west + 2) & 0xF;
        const Pid pid = fire_from_muzzle(game, 2, off, 9, true);
        const auto* missile = dynamic_cast<const ProjectileProcess*>(game.kernel().get(pid));
        CHECK(missile != nullptr);
        int min_distance = 1 << 20, frames = 0;
        for (; frames < 600 && game.kernel().get(pid); ++frames) {
            if (const auto* m = dynamic_cast<const ProjectileProcess*>(game.kernel().get(pid)))
                min_distance = std::min(min_distance, std::max(std::abs(m->position().x - player->x),
                                                               std::abs(m->position().y - player->y)));
            game.update(kFrameUs);
        }
        std::printf("missile: closest %d units, ended after %d frames, player hp %d -> %u\n", min_distance, frames, hp,
                    w.npc_hp(1));
        CHECK(min_distance < 64 && w.npc_hp(1) < hp);
    }
    // 8. Ricochet (fire type 5) off a shape with TYPEFLAG byte 1 bit 2
    // (0x2E) put east of the player: the flight turns back west.
    {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        const Item* player = w.get(1);
        const ObjId wall = w.create(0x2E, 0);
        w.pop_to_coords(player->x + 160, player->y + 16, player->z);
        const Pid pid = fire_from_muzzle(game, 1, 4, 5, false);
        const auto* p = dynamic_cast<const ProjectileProcess*>(game.kernel().get(pid));
        CHECK(p != nullptr && p->target().x > player->x);
        bool turned = false;
        for (int f = 0; f < 120 && game.kernel().get(pid); ++f) {
            game.update(kFrameUs);
            if (const auto* m = dynamic_cast<const ProjectileProcess*>(game.kernel().get(pid)))
                turned = turned || m->target().x < m->position().x;
        }
        std::printf("ricochet: wall %u, flight turned back %d\n", wall, turned);
        CHECK(wall != 0 && turned);
    }
    // 9. Special deaths of NPC 2 (shape 765): fire (type 6) -> burnt shape
    // 0x58F; explosive (type 4, 250) -> blown apart, shape 0x576; electric
    // (type 15) -> shape 0x59C.
    for (const auto [type, shape] : {std::pair{6, 0x58F}, std::pair{4, 0x576}, std::pair{15, 0x59C}}) {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        const Item* player = w.get(1);
        w.move_item(2, player->x + 128, player->y, player->z);
        apply_damage(game.kernel(), w, game.animations(), 2, 1, 12, 250, type);
        for (int f = 0; f < 600 && game.kernel().count_processes(2, ActorAnimProcess::kType) != 0; ++f)
            game.update(kFrameUs);
        std::printf("death: type %d -> shape 0x%X, dead %d, last action 0x%X\n", type, w.get(2)->shape, w.is_dead(2),
                    w.npc_word(2, npc::kLastAnim));
        CHECK(w.is_dead(2) && w.get(2)->shape == shape);
        CHECK(game.machine().faults().empty());
    }
    // 10. An explosive hit (type 4) throws an item with a weight (a pistol,
    // shape 0x32E) along the hit's direction (east, 4): its gravity process
    // gets vx = 2 x 10..25, vy = 0.
    {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        const Item* player = w.get(1);
        const ObjId gun = w.create(0x32E, 0);
        w.pop_to_coords(player->x + 96, player->y, player->z);
        apply_damage(game.kernel(), w, game.animations(), gun, 0, 4, 5, 4);
        const auto* g = dynamic_cast<const GravityProcess*>(game.kernel().find_process(gun, GravityProcess::kType));
        std::printf("thrown: velocity (%d, %d, %d)\n", g ? g->vx : 0, g ? g->vy : 0, g ? g->vz : 0);
        CHECK(g && g->vx >= 20 && g->vx <= 50 && g->vy == 0 && g->vz == 0);
    }
    // 11. Activities outside combat: NPC 8 loiters (activity 2), NPC 5 walks
    // straight on (activity 3, facing east), both east of the player.
    for (const auto [id, facing] : {std::pair{ObjId{8}, -1}, std::pair{ObjId{5}, 4}}) {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        const Item* player = w.get(1);
        if (facing >= 0) w.npc_record(id)[npc::kDirection] = static_cast<std::uint8_t>(facing);
        if (facing < 0) w.move_item(id, player->x + 160, player->y + 96, player->z);
        else w.move_item(id, player->x + 256, player->y - 40, player->z);
        const Item* npc = w.get(id);
        const int x0 = npc->x, y0 = npc->y;
        int moved = 0;
        for (int f = 0; f < 600; ++f) {
            game.update(kFrameUs);
            moved = std::max(moved, std::max(std::abs(npc->x - x0), std::abs(npc->y - y0)));
        }
        std::printf("activity: NPC %u moved up to %d units, now activity %u\n", id, moved, w.npc_word(id, npc::kActivity));
        CHECK(moved > 64);
        CHECK(game.machine().faults().empty());
    }
    // 12. A guard (activity 8) facing the player sees it and attacks (5).
    {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        const Item* player = w.get(1);
        auto r = w.npc_record(12);
        r[npc::kActivity] = 8;
        r[npc::kActivity + 1] = 0;
        r[npc::kDirection] = 12;  // west, towards the player
        w.move_item(12, player->x + 256, player->y - 40, player->z);
        const std::uint16_t before = w.npc_word(12, npc::kActivity);
        for (int f = 0; f < 240; ++f) game.update(kFrameUs);
        std::printf("activity: guard 12 activity %u -> %u, in combat %d\n", before, w.npc_word(12, npc::kActivity),
                    in_combat(w, 12));
        CHECK(before == 8 && w.npc_word(12, npc::kActivity) == 5 && in_combat(w, 12));
    }
    // 13. Actor::I_createActorCru: an NPC of the type of shape 765 made at a
    // spawner east of the player, facing 4, with its default activities,
    // hit points and an active weapon.
    {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        const Item* player = w.get(1);
        const auto types = dtable::NpcTypes::load(data / "STATIC/DTABLE.FLX");
        const int type = types.index(765);
        const ObjId egg = w.create(0x1, 0);  // any item will do as a spawner
        w.pop_to_coords(player->x + 256, player->y - 40, player->z);
        Item* e = w.get(egg);
        e->quality = 0x0107;      // default activity 1 = 1, record +0x63 = 7
        e->map_number = 0;        // from difficulty 1, first weapon
        e->npc_number = 0x32;     // facing 4, record +0x12 = 3
        const ObjId kind = w.create(0x1, 0);
        w.pop_to_coords(0, 0, 0);
        w.get(kind)->npc_number = static_cast<std::uint8_t>(type);
        w.get(kind)->quality = 0x0800;  // default activity 0 = 8
        w.get(kind)->map_number = 5;    // default activity 2 = 5
        const ObjId npc = create_actor(game.kernel(), w, game.animations(), types, egg, kind);
        const Item* n = w.get(npc);
        std::printf("create: type %d -> NPC %u shape %u at (%d, %d), dir %u, hp %u, activities %u %u %u, weapon %u\n",
                    type, npc, n ? n->shape : 0, n ? n->x : 0, n ? n->y : 0, w.npc_direction(npc), w.npc_hp(npc),
                    w.npc_word(npc, 6), w.npc_word(npc, 8), w.npc_word(npc, 10), w.active_weapon(npc));
        CHECK(n && n->shape == 765 && n->x == e->x && w.npc_direction(npc) == 4 && w.npc_hp(npc) == types.max_hp(type));
        CHECK(w.npc_word(npc, 6) == 8 && w.npc_word(npc, 8) == 1 && w.npc_word(npc, 10) == 5 && w.active_weapon(npc) != 0);
        for (int f = 0; f < 60; ++f) game.update(kFrameUs);
        CHECK(game.machine().faults().empty());
    }
    // 14. A monster egg (shape 0x24F, family 7) brought into the fast area
    // makes its NPC once: type of shape 765 (template inside the egg),
    // default activity 0 = 8 (guard), facing 2.
    {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        const Item* player = w.get(1);
        const auto types = dtable::NpcTypes::load(data / "STATIC/DTABLE.FLX");
        const ObjId egg = w.create(0x24F, 0);
        w.pop_to_coords(200, 200, 0);  // far from the fast area
        Item* e = w.get(egg);
        e->npc_number = 0x01;  // facing 2
        e->map_number = 0;
        const ObjId kind = w.create(0x1, 0);
        w.pop_to_end(egg);
        w.get(kind)->npc_number = static_cast<std::uint8_t>(types.index(765));
        w.get(kind)->quality = 0x0801;  // default activities 0 = 8, 1 = 1
        int before = 0;
        for (ObjId id = 2; id < 256; ++id) before += w.get(id) != nullptr;
        w.move_item(egg, player->x + 256, player->y - 40, player->z);
        ObjId made = 0;
        for (ObjId id = 2; id < 256; ++id)
            if (const Item* i = w.get(id); i && i->x == e->x && i->y == e->y && i->shape == 765) made = id;
        std::printf("egg: frame %u, NPC %u, activity %u, weapon %u, guard process %d\n", e->frame, made,
                    w.npc_word(made, npc::kActivity), w.active_weapon(made),
                    game.kernel().find_process(made, 0x25E) != nullptr);
        CHECK(e->frame == 1 && made != 0 && w.npc_word(made, npc::kActivity) == 8 && w.active_weapon(made) != 0);
        w.move_item(egg, 200, 200, 0);
        w.move_item(egg, player->x + 256, player->y - 40, player->z);  // a spent egg makes nothing
        int after = 0;
        for (ObjId id = 2; id < 256; ++id) after += w.get(id) != nullptr;
        CHECK(after == before + 1);
        for (int f = 0; f < 60; ++f) game.update(kFrameUs);
        CHECK(game.machine().faults().empty());
    }
    // 16. World::I_setControlledNPCNum: NPC 2 (a guard) takes the player's
    // commands (an arrow key moves it, not the player), the camera follows
    // it; then the player gets them back.
    {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        const Item* player = w.get(1);
        w.move_item(2, player->x + 256, player->y - 40, player->z);
        const Item start2 = *w.get(2), start1 = *player;
        set_controlled_npc(game.kernel(), w, game.animations(), 2);
        CHECK(w.state.controlled_npc == 2 && w.state.camera_target == 2);
        game.key(0x48, true, true, 0);
        for (int f = 0; f < 90; ++f) game.update(kFrameUs);
        game.key(0x48, true, false, 0);
        for (int f = 0; f < 30; ++f) game.update(kFrameUs);
        const Item& now2 = *w.get(2);
        std::printf("control: NPC 2 (%d, %d) -> (%d, %d), player moved %d\n", start2.x, start2.y, now2.x, now2.y,
                    std::abs(player->x - start1.x) + std::abs(player->y - start1.y));
        CHECK(now2.x != start2.x || now2.y != start2.y);
        CHECK(player->x == start1.x && player->y == start1.y);
        // Killed while controlled: no NPC is controlled any more.
        apply_damage(game.kernel(), w, game.animations(), 2, 1, 0, 250, 11);
        CHECK(w.is_dead(2) && w.state.controlled_npc == 0);
        set_controlled_npc(game.kernel(), w, game.animations(), 1);
        CHECK(w.state.controlled_npc == 1 && w.state.camera_target == 1);
        CHECK(game.machine().faults().empty());
    }
    // 15. Vargas's shield: NPC 6 made Vargas (0x3AC) loses no hit points, the
    // shield flashes (action 0x21) and loses the damage; a hit during the
    // flash does nothing; with the shield empty, hits hurt.
    {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        w.state.difficulty = 0;
        w.set_shape(*w.get(6), 0x3AC);
        const int hp = w.npc_hp(6);
        apply_damage(game.kernel(), w, game.animations(), 6, 1, 0, 30, 11);
        const auto* anim =
            dynamic_cast<const ActorAnimProcess*>(game.kernel().find_process(6, ActorAnimProcess::kType));
        const int after_first = w.state.vargas_shield;
        apply_damage(game.kernel(), w, game.animations(), 6, 1, 0, 30, 11);
        std::printf("vargas: hp %d -> %u, shield 5000 -> %d -> %d, action 0x%X\n", hp, w.npc_hp(6), after_first,
                    w.state.vargas_shield, anim ? anim->action() : 0);
        CHECK(w.npc_hp(6) == hp && after_first == 5000 - 30 && w.state.vargas_shield == after_first);
        CHECK(anim && anim->action() == 0x21);
        w.state.vargas_shield = 0;
        for (int f = 0; f < 120; ++f) game.update(kFrameUs);
        apply_damage(game.kernel(), w, game.animations(), 6, 1, 0, 30, 11);
        CHECK(w.npc_hp(6) == hp - 30);
    }
    // 3. Shields: with shield type 1 (record +0x66), a pistol shot (fire type
    // 11: cost 80, types 7) takes 80 energy instead of hit points; without
    // enough energy it hurts. Immortality (+0x13 bit 1) stops everything.
    {
        Game game(data);
        game.load_save(13);
        World& w = game.world();
        auto r = w.npc_record(1);
        r[0x66] = 1;
        r[0x67] = 0;
        const int hp = w.npc_hp(1), energy = w.npc_mana(1);
        apply_damage(game.kernel(), w, game.animations(), 1, 2, 0, 20, 11);
        std::printf("enemies: shield: hp %d -> %u, energy %d -> %u\n", hp, w.npc_hp(1), energy, w.npc_mana(1));
        CHECK(w.npc_hp(1) == hp && w.npc_mana(1) == energy - 80);
        apply_damage(game.kernel(), w, game.animations(), 1, 2, 0, 20, 11);  // 50 left: not enough
        CHECK(w.npc_hp(1) == hp - 4 && w.npc_mana(1) == energy - 80);   // 20 / 5 at difficulty 1
        r[0x66] = 0;
        r[npc::kFlags13] |= 0x02;
        apply_damage(game.kernel(), w, game.animations(), 1, 2, 0, 20, 11);
        CHECK(w.npc_hp(1) == hp - 4);
    }
}

// The alert (World::I_setAlertActive): using an alarm box (ALARMBOX, shape
// 0x477) on a map raises the alert through the scripts (FREE event 0x3B);
// alarm lights and boxes on the map change frame with it.
void test_alert(const std::filesystem::path& data) {
    for (int save = 1; save <= 13; ++save) {
        Game game(data);
        game.load_save(save);
        World& w = game.world();
        ObjId box = 0;
        int lights = 0;
        w.for_each([&](const Item& i) {
            if (i.parent != 0) return;
            if (i.shape == 0x477 && box == 0 && i.frame < 2) box = i.id;
            if (i.shape == 0x21 || i.shape == 0x49 || i.shape == 0x174 || i.shape == 0x271) lights += i.frame == 0;
        });
        if (box == 0 || lights == 0 || w.state.alert_active) continue;
        const int box_frame = w.get(box)->frame;
        const std::uint8_t raise[2] = {1, 0};  // equip(1): raise the alert
        game.machine().start_event(0x477, 0x0A, box, raise);
        for (int f = 0; f < 120; ++f) game.update(kFrameUs);
        int lit = 0;
        w.for_each([&](const Item& i) {
            if (i.parent == 0 && (i.shape == 0x21 || i.shape == 0x49 || i.shape == 0x174 || i.shape == 0x271))
                lit += i.frame == 1;
        });
        std::printf("alert: save %d, box %u frame %u -> %u, alert %d, %d lights off, %d lit, %zu faults\n", save, box,
                    box_frame, w.get(box)->frame, w.state.alert_active, lights, lit, game.machine().faults().size());
        for (const auto& [n, c] : game.machine().unbound_intrinsic_calls()) std::printf("  unbound 0x%03X x%u\n", n, c);
        CHECK(w.state.alert_active && w.get(box)->frame == box_frame + 2 && lit >= lights);
        game.machine().start_event(0x477, 0x01, box);  // use(): switch it off
        for (int f = 0; f < 120; ++f) game.update(kFrameUs);
        int still = 0;
        w.for_each([&](const Item& i) {
            if (i.parent == 0 && (i.shape == 0x21 || i.shape == 0x49 || i.shape == 0x174 || i.shape == 0x271))
                still += i.frame == 1;
        });
        std::printf("alert: switched off: alert %d, box frame %u, %d lit\n", w.state.alert_active, w.get(box)->frame,
                    still);
        CHECK(!w.state.alert_active && w.get(box)->frame == box_frame && still == 0);
        CHECK(game.machine().faults().empty());
        return;
    }
    CHECK(!"no save with an alarm box");
}

int main(int argc, char** argv) {
    const std::filesystem::path data = argc > 1 ? argv[1] : "data";
    if (!std::filesystem::exists(data / "SAVEGAME/SAVEGAME.1")) {
        std::printf("no game data, skipped\n");
        return 0;
    }
    test_load_and_run(data);
    test_alert(data);
    test_player_walks(data);
    test_camera_follows(data);
    test_camera_sweep(data);
    test_status_bar(data);
    test_keys_and_cursor(data);
    test_menu(data);
    test_load_from_menu(data);
    test_fades(data);
    test_credits(data);
    test_options(data);
    test_firing(data);
    test_stations(data);
    test_scenery_use(data);
    test_computer(data);
    test_keypad(data);
    test_monitor(data);
    test_security_camera(data);
    test_inventory_keys(data);
    test_item_selection(data);
    test_message_box(data);
    test_function_keys(data);
    test_mouse_in_play(data);
    test_hard_landing(data);
    test_shot_kills(data);
    test_aiming(data);
    test_aim_mode(data);
    test_enemies(data);
    test_new_game(data);
    test_movies(data);
    std::printf(failures ? "%d failure(s)\n" : "all game checks passed\n", failures);
    return failures ? 1 : 0;
}

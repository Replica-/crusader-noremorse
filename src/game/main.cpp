// crusader: runs the game from a saved game (milestone 1: the world runs, the
// camera moves with the arrow keys).
//
//   crusader [data | game.iso] [--save N | --new-game]  (neither: the start-up movies, then the menu) [--movie NAME | --movie-overlay NAME] [--frames N [--scroll DX DY] [--walk] [--fire] [--use ID] [--menu | --menu-load | --menu-credits | --menu-options | --menu-gamepad | --menu-new] --png out.png [--wav out.wav]]
//
// With --png, runs N frames of 1/60 s without a window, moving the camera by
// (DX, DY) usecode units per frame or making the player walk, and writes the
// last frame; --wav also writes the sound mixed meanwhile. --new-game starts
// the first mission instead of loading a saved game. --use uses an item
// (as Enter on the selected item) before the first frame. --movie plays a
// cutscene (FLICS/NAME.AVI, as MovieGump::I_playMovieCutscene) right away,
// --movie-overlay as I_playMovieOverlay.
//
// Controls of the original (CRUSADER.C 12CF): arrows (shift runs), keypad,
// Ctrl / Alt moves; see engine/world/avatar_mover.hpp. Escape opens the menu
// (keyboard or left mouse button; "quit" leaves).

#include "core/iso_image.hpp"
#include "core/png_writer.hpp"
#include "crusader_version.hpp"
#include "engine/world/inventory.hpp"
#include "game/game.hpp"
#include "platform/sdl_audio.hpp"
#include "platform/sdl_gamepad.hpp"
#include "platform/sdl_input.hpp"
#include "platform/sdl_presenter.hpp"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <filesystem>
#include <string>
#include <vector>

using namespace crusader;

namespace {

constexpr std::uint32_t kFrameUs = 1'000'000 / 60;

// @orig none: the port's window title (taskbar, title bar)
constexpr const char* kWindowTitle = "Crusader: No Remorse — QOL, Enhancements, OpenGL";

// @orig none: start-up diagnostics of the port
std::string diagnostics(Game& game, const RenderStats& stats, double fps) {
    std::size_t unbound = 0;
    for (const auto& [n, calls] : game.machine().unbound_intrinsic_calls()) unbound += calls;
    const GameState& s = game.world().state;
    char text[256];
    std::snprintf(text, sizeof text,
                  "crusader  camera (%d, %d, %d)  %.0f fps  processes %zu  items %zu drawn %zu  "
                  "usecode faults %zu  missing intrinsic calls %zu",
                  s.camera_x, s.camera_y, s.camera_z, fps, game.kernel().process_count(), game.world().item_count(),
                  stats.drawn, game.machine().faults().size(), unbound);
    return text;
}

// Mono 16-bit PCM WAV.
// @orig none: WAV export for the tools
void write_wav(const std::string& path, const std::vector<std::int16_t>& samples, int rate) {
    std::ofstream out(path, std::ios::binary);
    auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);
    out.write("RIFF", 4);
    u32(36 + bytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate * 2));
    u16(2);
    u16(16);
    out.write("data", 4);
    u32(bytes);
    out.write(reinterpret_cast<const char*>(samples.data()), bytes);
}

void print_faults(Game& game) {
    for (const auto& f : game.machine().faults()) {
        std::fprintf(stderr, "usecode fault: %s (class 0x%04X ip 0x%04X)\n", f.message.c_str(), f.class_id, f.ip);
    }
}

}  // namespace

// @orig none: command line and SDL start-up of the port
int main(int argc, char** argv) {
    SDL_SetMainReady();
    std::filesystem::path data = "data", iso;
    int save = 1, frames = 0;
    bool new_game = false, save_given = false, fullscreen = false, debug_title = false;
    int png_width = Game::kScreenWidth, png_height = Game::kScreenHeight;
    bool classic = false;  // --classic: the original 640 x 480 view, stretched to the window
    bool skip_movies = false;  // --skip-movies: no cutscenes or intro videos
    bool show_fps = true;  // the fps / renderer counter, top right (--no-fps hides it)
    std::string shot_file;     // --shot FILE [--shot-after SECONDS]: save the picture of the window (a BMP) and quit
    double shot_after = 6.0;
    double pointer_fx = -1, pointer_fy = -1;  // --pointer FX FY: the pointer kept at this fraction of the window
    bool demo_blood = false, demo_hit_bar = false, demo_hover = false;
    int demo_walk_dx = 0, demo_walk_dy = 0, demo_jump_dx = 0, demo_jump_dy = 0;
    bool demo_jump_go = false;
    int start_wheel = 0;  // --wheel 0 / 1: start with the weapon / item wheel open
    int mission = 1;         // --mission N: with --new-game, start mission N
    bool all_items = false;  // --all-items: with --new-game, every weapon, ammunition and item
    bool start_map = false;  // --map: start with the mission map open
    bool path_test = false, path_gun = false;  // --path-test dx dy [--gun]: developer trace
    bool start_gun = false;  // --gun: start with the weapon drawn
    int path_dx = 0, path_dy = 0;
    bool keyboard_mouse = false;  // --keyboard-mouse: the keyboard and mouse mode, whatever CRUSADER.CFG says
    double gamma = 1.0;
    std::int32_t scroll_x = 0, scroll_y = 0;
    bool walk = false, fire = false, menu = false, menu_load = false, menu_credits = false, menu_options = false, menu_gamepad = false,
         menu_new = false, controls_screen = false, colours_screen = false, f1_test = false;
    std::string png, wav, movie;
    int use = 0;
    MovieKind movie_kind = MovieKind::kCutscene;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("crusader %s\n", CRUSADER_VERSION);
            return 0;
        }
        if (std::strcmp(argv[i], "--save") == 0 && i + 1 < argc) save = std::stoi(argv[++i]), save_given = true;
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = std::stoi(argv[++i]);
        else if (std::strcmp(argv[i], "--png") == 0 && i + 1 < argc) png = argv[++i];
        else if (std::strcmp(argv[i], "--wav") == 0 && i + 1 < argc) wav = argv[++i];
        else if (std::strcmp(argv[i], "--new-game") == 0) new_game = true;
        else if (std::strcmp(argv[i], "--fullscreen") == 0) fullscreen = true;
        else if (std::strcmp(argv[i], "--debug-title") == 0) debug_title = true;  // diagnostics in the window title
        else if (std::strcmp(argv[i], "--classic") == 0) classic = true;
        else if (std::strcmp(argv[i], "--skip-movies") == 0) skip_movies = true;
        else if (std::strcmp(argv[i], "--no-fps") == 0) show_fps = false;
        else if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot_file = argv[++i];  // save the picture and quit
        else if (std::strcmp(argv[i], "--shot-after") == 0 && i + 1 < argc) shot_after = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "--pointer") == 0 && i + 2 < argc) { pointer_fx = std::stod(argv[++i]); pointer_fy = std::stod(argv[++i]); }
        else if (std::strcmp(argv[i], "--demo-blood") == 0) demo_blood = true;
        else if (std::strcmp(argv[i], "--demo-hitbar") == 0) demo_hit_bar = true;
        else if (std::strcmp(argv[i], "--demo-hover") == 0) demo_hover = true;
        else if (std::strcmp(argv[i], "--demo-jump") == 0 && i + 2 < argc) { demo_jump_dx = std::stoi(argv[++i]); demo_jump_dy = std::stoi(argv[++i]); }
        else if (std::strcmp(argv[i], "--demo-jump-go") == 0) demo_jump_go = true;
        else if (std::strcmp(argv[i], "--demo-walk") == 0 && i + 2 < argc) { demo_walk_dx = std::stoi(argv[++i]); demo_walk_dy = std::stoi(argv[++i]); }
        else if (std::strcmp(argv[i], "--wheel") == 0 && i + 1 < argc) start_wheel = std::stoi(argv[++i]) + 1;
        else if (std::strcmp(argv[i], "--mission") == 0 && i + 1 < argc) mission = std::stoi(argv[++i]);
        else if (std::strcmp(argv[i], "--all-items") == 0) all_items = true;
        else if (std::strcmp(argv[i], "--map") == 0) start_map = true;
        else if (std::strcmp(argv[i], "--path-test") == 0 && i + 2 < argc) {  // dx dy [gun]: trace of W towards a point
            path_test = true;
            path_dx = std::stoi(argv[++i]);
            path_dy = std::stoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--gun") == 0) path_gun = start_gun = true;
        else if (std::strcmp(argv[i], "--keyboard-mouse") == 0) keyboard_mouse = true;
        else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {  // with --png: the screen's size, e.g. 960x540
            std::sscanf(argv[++i], "%dx%d", &png_width, &png_height);
        }
        else if (std::strcmp(argv[i], "--gamma") == 0 && i + 1 < argc) gamma = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "--movie") == 0 && i + 1 < argc) movie = argv[++i];
        else if (std::strcmp(argv[i], "--movie-overlay") == 0 && i + 1 < argc) {
            movie = argv[++i];
            movie_kind = MovieKind::kOverlay;
        }
        else if (std::strcmp(argv[i], "--walk") == 0) walk = true;
        else if (std::strcmp(argv[i], "--fire") == 0) fire = true;
        else if (std::strcmp(argv[i], "--use") == 0 && i + 1 < argc) use = std::stoi(argv[++i]);
        else if (std::strcmp(argv[i], "--menu") == 0) menu = true;
        else if (std::strcmp(argv[i], "--menu-load") == 0) menu = menu_load = true;
        else if (std::strcmp(argv[i], "--menu-credits") == 0) menu = menu_credits = true;
        else if (std::strcmp(argv[i], "--menu-options") == 0) menu = menu_options = true;
        else if (std::strcmp(argv[i], "--menu-gamepad") == 0) menu = menu_options = menu_gamepad = true;
        else if (std::strcmp(argv[i], "--controls-screen") == 0) controls_screen = true;
        else if (std::strcmp(argv[i], "--colours-screen") == 0) colours_screen = true;
        else if (std::strcmp(argv[i], "--f1") == 0) f1_test = true;
        else if (std::strcmp(argv[i], "--menu-new") == 0) menu = menu_new = true;
        else if (std::strcmp(argv[i], "--scroll") == 0 && i + 2 < argc) {
            scroll_x = std::stoi(argv[++i]);
            scroll_y = std::stoi(argv[++i]);
        }
        else if (std::filesystem::path p = argv[i]; p.extension() == ".iso" || p.extension() == ".ISO") iso = p;
        else data = argv[i];
    }

    try {
        // The data folder holds an install of the game, or an ISO of the CD
        // that is extracted into it on the first launch.
        prepare_game_data(data, iso);
        if (!std::filesystem::exists(data / "STATIC")) {
            throw std::runtime_error("no game data in '" + data.string() +
                                     "': copy the files of an install of the game (French 1.08 or US 1.23) there, or its .iso image");
        }
        Game game(data);
        game.set_port_version(CRUSADER_VERSION);
        game.set_skip_movies(skip_movies);
        if (keyboard_mouse) game.force_keyboard_mouse();
        // No game asked for: the start-up, as when the original is launched
        // (the movies, then the menu). The tools (--png, --movie) keep
        // loading save 1.
        const bool start_up = !new_game && !save_given && png.empty() && movie.empty();
        if (new_game) game.new_game(0, mission, all_items);
        else if (start_up) game.start_up();
        else game.load_save(save);
        if (start_map) game.set_map(true);
        if (start_wheel) game.request_wheel(start_wheel - 1);
        game.set_demo(demo_blood, demo_hit_bar, demo_walk_dx, demo_walk_dy, demo_hover);
        game.set_demo_jump(demo_jump_dx, demo_jump_dy, demo_jump_go);
        if (colours_screen) game.request_colours_screen();
        if (start_gun && !path_test) game.request_draw();
        if (path_test) {
            for (int i = 0; i < 60; ++i) game.update(kFrameUs);
            game.debug_path_trace(path_dx, path_dy, path_gun, 240);
            return 0;
        }
        if (!movie.empty()) game.play_movie(movie, movie_kind);
        Surface8 screen(png_width, png_height);
        RenderStats stats;

        if (!png.empty()) {
            // --walk: the up arrow is held for the whole run.
            if (walk) game.key(0x48, true, true, 0);
            game.capture_sound(!wav.empty());
            if (use) use_item(game.world(), static_cast<ObjId>(use));
            for (int i = 0; i < frames; ++i) {
                if (scroll_x || scroll_y) game.move_camera(scroll_x, scroll_y);
                // --fire: space tapped every 40 frames
                if (fire && i % 40 == 0) {
                    game.key(0x39, false, true, 0, ' ');
                    game.key(0x39, false, false, 0);
                }
                game.update(kFrameUs);
            }
            if (!wav.empty()) write_wav(wav, game.captured_sound(), AsylumMixer::kMixRate);
            if (menu) {  // --menu: Escape, as pressed in play
                game.key(0x01, false, true, 0, 0x1B);
                game.mouse_move(320, 240);
            }
            if (menu_load) {  // then down to "load" and Enter
                game.key(0x50, true, true, 0);
                game.key(0x1C, false, true, 0, 0x0D);
            }
            if (menu_credits) {  // right, down to "credits", Enter, then --frames N
                game.key(0x4D, true, true, 0);
                game.key(0x4D, true, false, 0);
                game.key(0x50, true, true, 0);
                game.key(0x50, true, false, 0);
                game.key(0x1C, false, true, 0, 0x0D);
                game.key(0x1C, false, false, 0);
                for (int i = 0; i < frames; ++i) game.update(kFrameUs);
            }
            if (menu_options) {  // right to "options", Enter
                game.key(0x4D, true, true, 0);
                game.key(0x4D, true, false, 0);
                game.key(0x1C, false, true, 0, 0x0D);
                game.key(0x1C, false, false, 0);
            }
            if (menu_gamepad) {  // then the "manette" button, clicked
                game.mouse_move(440, 330);
                game.mouse_button(1, true, 440, 330);
                game.mouse_button(1, false, 440, 330);
            }
            if (menu_new) {  // Enter on "new game": the difficulty screen
                game.key(0x1C, false, true, 0, 0x0D);
                game.key(0x1C, false, false, 0);
            }
            if (f1_test) {  // F1, then Enter: the controls screen, then the original help (a second picture)
                game.key(0x3B, false, true, 0);
                game.key(0x3B, false, false, 0);
                for (int i = 0; i < 5; ++i) game.update(kFrameUs);
                game.render(screen, &stats);
                write_png(png, screen, game.palette());
                game.key(0x1C, false, true, 0, 0x0D);
                game.key(0x1C, false, false, 0);
                for (int i = 0; i < 5; ++i) game.update(kFrameUs);
            }
            if (controls_screen) game.show_controls_screen();
            game.render(screen, &stats);
            write_png(png, screen, game.palette());
            std::printf("%s\n", diagnostics(game, stats, 0).c_str());
            for (const auto& [n, calls] : game.machine().unbound_intrinsic_calls()) {
                std::printf("  missing intrinsic 0x%03X: %u calls\n", n, calls);
            }
            print_faults(game);
            return game.machine().faults().empty() ? 0 : 2;
        }

        // The options screen saves its settings, as the original does.
        game.set_config_file(data / "CRUSADER.CFG");
        SdlPresenter presenter(kWindowTitle, Game::kScreenWidth, Game::kScreenHeight, 2, fullscreen);
        presenter.set_gamma(gamma);
        presenter.set_font(game.font_atlas());
        if (!classic && !fullscreen) presenter.maximize();
        // Native resolution: the game is drawn at the largest whole fraction of
        // the window that is still 640 x 480 or more, so the pixels keep their
        // shape and a wider window shows more of the surroundings.
        auto fit_screen = [&]() {
            int w = 0, h = 0;
            presenter.output_size(&w, &h);
            if (classic || !game.config().native_resolution) w = Game::kScreenWidth, h = Game::kScreenHeight;
            int scale = std::max(1, std::min(w / Game::kScreenWidth, h / Game::kScreenHeight));
            game.set_view_zoom_limit(scale);
            if (const int zoom = game.config().view_zoom) scale = std::min(scale, zoom);
            const int vw = std::max(Game::kScreenWidth, w / scale), vh = std::max(Game::kScreenHeight, h / scale);
            if (vw != screen.width || vh != screen.height) {
                screen = Surface8(vw, vh);
                presenter.resize(vw, vh);
            }
        };
        std::unique_ptr<SdlAudio> audio;
        if (game.audio()) {
            audio = std::make_unique<SdlAudio>(game.audio()->mixer);
            game.set_audio_device(audio->ok());
        }
        SDL_ShowCursor(SDL_DISABLE);  // the game draws its own cursor
        // In play the game puts the mouse back at (320, 200) after each move
        // (world_mouse.hpp): SDL's relative mode gives the same motion
        // without warping the system pointer.
        bool relative_mouse = false;
        Gamepad gamepad;
        gamepad.init();
        const bool pad_debug = std::getenv("CRUSADER_PAD_DEBUG") != nullptr;  // see sdl_gamepad.cpp
        if (pad_debug) std::setvbuf(stdout, nullptr, _IOLBF, 0);
        gamepad.set_settings(&game.config().pad);
        std::uint64_t last = SDL_GetPerformanceCounter();
        const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
        double fps = 0;
        double work_ms = 0;                       // CPU time of a pass (update + draw), smoothed
        double tick_rate = 0;                     // game-loop (kernel) ticks per second
        std::uint32_t tick_mark = 0;
        std::uint64_t tick_mark_at = SDL_GetPerformanceCounter();
        bool shot_requested = false;
        bool running = true;
        while (running) {
            const std::uint64_t pass_start = SDL_GetPerformanceCounter();
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) running = false;
                if (gamepad.feed(ev)) {
                    if (gamepad.quit_requested()) running = false;
                    continue;
                }
                // Alt+Enter: fullscreen on / off, not sent to the game.
                if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && ev.key.keysym.sym == SDLK_RETURN &&
                    (ev.key.keysym.mod & KMOD_ALT)) {
                    if (ev.type == SDL_KEYDOWN && !ev.key.repeat) presenter.toggle_fullscreen();
                    continue;
                }
                if (auto k = translate_key(ev)) {
                    if (pad_debug) std::printf("[key] sdl keyboard %02x%s %s\n", k->scancode, k->extended ? " ext" : "", k->pressed ? "down" : "up");
                    game.key(k->scancode, k->extended, k->pressed, k->flags, k->ascii);
                }
                // The weapon / item wheel takes the mouse while it is open: move to pick, left click to choose, right click to close.
                if (game.wheel_overlay().on) {
                    if (auto m = translate_mouse(ev)) {
                        game.set_pointer(m->x, m->y);  // the arrow follows the mouse
                        if (m->kind == MouseEvent::Kind::Move) game.wheel_mouse(0, m->x, m->y);
                        else if (m->kind == MouseEvent::Kind::ButtonDown && m->button == 1) game.wheel_mouse(1, m->x, m->y);
                        else if (m->kind == MouseEvent::Kind::ButtonDown && m->button == 2) game.wheel_mouse(2, m->x, m->y);
                        continue;
                    }
                    if (ev.type == SDL_MOUSEWHEEL) continue;
                }
                // The mission map (M) takes the mouse while it is open: drag to pan, wheel to zoom, middle click to recentre.
                if (game.map_overlay().on) {
                    static int map_mx = 0, map_my = 0;
                    if (ev.type == SDL_MOUSEWHEEL) {
                        game.map_mouse(3, map_mx, map_my, ev.wheel.y);
                        continue;
                    }
                    if (auto m = translate_mouse(ev)) {
                        map_mx = m->x, map_my = m->y;
                        game.set_pointer(m->x, m->y);  // the arrow follows the mouse over the map
                        if (m->kind == MouseEvent::Kind::Move) game.map_mouse(0, m->x, m->y, 0);
                        else if (m->button == 1) game.map_mouse(m->kind == MouseEvent::Kind::ButtonDown ? 1 : 2, m->x, m->y, 0);
                        else if (m->button == 3 && m->kind == MouseEvent::Kind::ButtonDown) game.map_mouse(4, m->x, m->y, 0);
                        continue;
                    }
                }
                if (auto m = translate_mouse(ev)) {
                    if (relative_mouse) {
                        const bool move = m->kind == MouseEvent::Kind::Move;
                        m->x = game.cursor().x() + (move ? m->dx : 0);
                        m->y = game.cursor().y() + (move ? m->dy : 0);
                    } else {  // the menus are 640 x 480, centred in the screen
                        if (game.free_pointer()) game.set_pointer(m->x, m->y);  // keyboard and mouse mode
                        m->x -= game.ui_x();
                        m->y -= game.ui_y();
                    }
                    if (m->kind == MouseEvent::Kind::Move) game.mouse_move(m->x, m->y);
                    else game.mouse_button(m->button, m->kind == MouseEvent::Kind::ButtonDown, m->x, m->y);
                }
            }
            const std::uint64_t now = SDL_GetPerformanceCounter();
            const double seconds = static_cast<double>(now - last) / freq;
            last = now;
            fps = fps * 0.95 + (seconds > 0 ? 0.05 / seconds : 0);

            // The controller: A/B confirm/cancel in menus, its stick moves the mouse.
            gamepad.set_menu(!game.mouse_in_play());
            gamepad.set_capture(game.waiting_for_pad());
            gamepad.set_directional(game.config().directional_move);
            gamepad.set_directional_aim(game.config().directional_aim);
            double aim_deg = 0;
            const bool aim_pushed = gamepad.aim_stick(&aim_deg);  // (argument order is unspecified)
            game.set_aim_stick(aim_pushed, aim_deg);
            for (int b : gamepad.take_captured()) game.pad_button(b);
            for (const KeyEvent& k : gamepad.take_keys()) {
                if (pad_debug) std::printf("[key] pad %02x%s %s\n", k.scancode, k.extended ? " ext" : "", k.pressed ? "down" : "up");
                game.key(k.scancode, k.extended, k.pressed, k.flags, k.ascii);
            }
            if (int dx, dy; (gamepad.mouse_motion(seconds, &dx, &dy), dx || dy)) {
                game.mouse_move(game.cursor().x() + dx, game.cursor().y() + dy);
            }

            if (pointer_fx >= 0 && !demo_hover) game.set_pointer(static_cast<int>(pointer_fx * screen.width), static_cast<int>(pointer_fy * screen.height));
            game.update(static_cast<std::uint32_t>(seconds * 1'000'000));
            if (game.quit_requested()) running = false;
            // (keyboard and mouse mode: the pointer is absolute, not relative)
            if ((game.mouse_in_play() && !game.config().keyboard_mouse) != relative_mouse) {
                relative_mouse = game.mouse_in_play() && !game.config().keyboard_mouse;
                SDL_SetRelativeMouseMode(relative_mouse ? SDL_TRUE : SDL_FALSE);
                // Back in a menu: the system pointer where the game's is.
                if (!relative_mouse) presenter.warp_mouse(game.cursor().x() + game.ui_x(), game.cursor().y() + game.ui_y());
            }
            fit_screen();
            game.render(screen, &stats);
            if (debug_title) {
                int ow = 0, oh = 0;
                presenter.output_size(&ow, &oh);
                char size[64];
                std::snprintf(size, sizeof size, "  view %dx%d in %dx%d", screen.width, screen.height, ow, oh);
                presenter.set_title(diagnostics(game, stats, fps) + size);
            }
            if (show_fps) {
                const std::uint64_t t = SDL_GetPerformanceCounter();
                work_ms = work_ms * 0.9 + 0.1 * (static_cast<double>(t - pass_start) * 1000.0 / freq);
                const double since = static_cast<double>(t - tick_mark_at) / freq;
                if (since >= 0.5) {
                    const std::uint32_t ticks = game.kernel().ticks();
                    tick_rate = (ticks - tick_mark) / since;
                    tick_mark = ticks;
                    tick_mark_at = t;
                }
                char line[128];
                std::snprintf(line, sizeof line, "%.0f FPS  %s (%s)", fps, presenter.software_renderer() ? "SW" : "HW",
                              presenter.renderer_name().c_str());
                const float rx = static_cast<float>(screen.width) - 8.0f;
                presenter.text(rx, 18.0f, line, 255, 255, 255, true);
                // The game loop itself: the time its update and draw take (what it could run at without
                // waiting for the display), and the game's own tick rate.
                std::snprintf(line, sizeof line, "LOOP %.1f ms (%.0f/s max)  TICKS %.0f/s", work_ms,
                              work_ms > 0 ? 1000.0 / work_ms : 0.0, tick_rate);
                presenter.text(rx, 34.0f, line, 210, 235, 255, true);
                if (const std::string path = game.path_status(); !path.empty()) presenter.text(rx, 50.0f, path, 120, 255, 255, true);
            }
            presenter.set_debug_path(game.path_debug_screen());
            presenter.set_map(game.map_overlay().on ? &game.map_overlay() : nullptr);
            presenter.set_wheel(game.wheel_overlay().on ? &game.wheel_overlay() : nullptr);
            presenter.add_wheel_icons(game.take_wheel_icons());
            presenter.set_jump(game.jump_preview().on ? &game.jump_preview() : nullptr);
            presenter.set_colours(game.colours_overlay());
            presenter.set_player_layer(game.player_layer());
            {
                std::vector<SdlPresenter::BloodDot> dots;
                for (const Game::BloodDotScreen& b : game.blood_screen()) dots.push_back({b.x, b.y, b.x0, b.y0, b.size, b.alpha, b.kind});
                presenter.set_blood(std::move(dots));
            }
            {
                const Game::PointerOverlay p = game.pointer_overlay();
                presenter.set_pointer(p.show, p.x, p.y, p.hover, p.ring);
            }
            {
                std::vector<SdlPresenter::HitBar> bars;
                for (const Game::HitBarScreen& b : game.hit_bars_screen()) bars.push_back({b.x, b.y, b.fraction, b.alpha});
                presenter.set_hit_bars(std::move(bars));
            }
            {
                const Game::LaserScreen laser = game.laser_screen();
                presenter.set_laser(laser.on, laser.hit, laser.x0, laser.y0, laser.x1, laser.y1, laser.spread_x, laser.spread_y);
            }
            if (!shot_file.empty() && SDL_GetTicks() / 1000.0 >= shot_after && !shot_requested) {
                presenter.request_screenshot(shot_file);
                shot_requested = true;
            }
            presenter.present(screen, game.palette());
            if (shot_requested && presenter.screenshot_done()) running = false;
        }
        print_faults(game);
        SDL_Quit();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "crusader", e.what(), nullptr);
        return 1;
    }
}

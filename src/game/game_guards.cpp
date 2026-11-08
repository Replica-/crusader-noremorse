// Port addition: the guards a trigger will make, shown waiting where they will appear before it fires.
//
// The original makes most of a level's guards only when the player walks into a trigger egg (family 4, its event 7):
// the egg's script calls Actor::I_createActorCru on a pair of spawn markers (shape 0x4D0: frame 0 the place and
// facing, frame 1 the NPC type) and then destroys them. Monster eggs (family 7) make theirs when they enter the fast
// area. Both popped in on screen, the port's view being larger than the original's fast area.
//
// Which markers a trigger egg uses is in its script, not in the map: crusader --probe-guards walks the player onto
// each trigger egg of the map in turn and writes down the places of the guards made while it stands there (delayed
// ones included) into assets/guards/map_NNN.cfg ("guard X Y Z SHAPE" a line; commit them). Guards that beam in are
// left out: a sprite effect (the teleport's flash) starts where they stand when they are made. In the game, each listed place whose frame 0 marker is still there (the trigger has not
// fired: it destroys the pair) shows its guard standing, facing its way: a picture only, no NPC, nothing runs. When
// the trigger fires, the real guard appears in its place.
#include "game/game.hpp"

#include "engine/world/actor_create.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace crusader {

namespace {

constexpr std::uint16_t kSpawnMarker = 0x4D0;   // Actor::I_createActorCru's egg and kind (frames 0 and 1)
constexpr std::uint32_t kProbeEffectTicks = 90;  // a sprite effect this near in time to a guard's making is its own
constexpr std::int32_t kEffectReach = 64;        // and this near in place (usecode units)
constexpr std::uint32_t kProbeSettleUs = 4'000'000, kProbeStepUs = 3'000'000;

std::filesystem::path guards_file(int map) {
    char name[32];
    std::snprintf(name, sizeof name, "map_%03d.cfg", map);
    return std::filesystem::current_path() / "assets" / "guards" / name;
}

bool on_pad(const std::vector<std::pair<std::int32_t, std::int32_t>>& pads, std::int32_t x, std::int32_t y,
            std::int32_t reach) {
    return std::any_of(pads.begin(), pads.end(),
                       [&](const auto& p) { return std::abs(p.first - x) < reach && std::abs(p.second - y) < reach; });
}

}  // namespace

// The teleporter pads of the map: where guards beam in.
// @orig none: port-only waiting guards
std::vector<std::pair<std::int32_t, std::int32_t>> Game::teleporter_pads() {
    std::vector<std::pair<std::int32_t, std::int32_t>> pads;
    world_->for_each([&](const Item& item) {
        if (item.off_map || item.parent != 0) return;
        const std::string& name = object_name(item.shape);
        if (name.find("TELEPAD") != std::string::npos || name.find("TELEPORT") != std::string::npos) pads.emplace_back(item.x, item.y);
    });
    return pads;
}

// The places of assets/guards/map_NNN.cfg, read once.
// @orig none: port-only waiting guards
const std::vector<Game::GuardSpawn>& Game::guard_spawns(int map) {
    auto found = guard_spawns_.find(map);
    if (found != guard_spawns_.end()) return found->second;
    std::vector<GuardSpawn> spawns;
    std::ifstream in(guards_file(map));
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string kind;
        GuardSpawn g;
        if (words >> kind && kind == "guard" && words >> g.x >> g.y >> g.z >> g.shape) spawns.push_back(g);
    }
    return guard_spawns_.emplace(map, std::move(spawns)).first->second;
}

// The pictures of the guards waiting: the monster eggs not hatched yet and the listed trigger guards not made yet.
// @orig none: port-only waiting guards
std::span<const Item> Game::update_waiting_guards() {
    waiting_guards_.clear();
    if (!world_ || editor_ || guard_probe_ || !config_.waiting_guards) return waiting_guards_;  // (off by default)
    const int map = world_->current_map();
    const std::vector<GuardSpawn>& listed = guard_spawns(map);
    std::vector<const Item*> eggs, markers;
    world_->for_each([&](const Item& item) {
        if (item.off_map || item.parent != 0 || (item.flags & kItemContained)) return;
        if (item.shape == kSpawnMarker && item.frame == 0 && !listed.empty()) {
            markers.push_back(&item);
            return;
        }
        const ShapeInfo* info = world_->shape_info(item.shape);
        if (info && info->family == kMonsterEggFamily && item.frame != 1) eggs.push_back(&item);  // (frame 1: hatched)
    });
    const auto guard = [&](std::uint16_t shape, const Item& egg) {  // as World::create_npc gets it, at the egg's place
        Item g;
        g.shape = shape;
        g.frame = static_cast<std::uint16_t>(((egg.npc_number & 0xF) * 2 + 4) & 0xF);
        g.x = egg.x, g.y = egg.y, g.z = egg.z & 0xFF;
        waiting_guards_.push_back(g);
    };
    const auto too_hard = [&](const Item& egg) { return (egg.map_number & 3) + 1 > world_->state.difficulty; };
    if (!eggs.empty()) {
        const auto pads = teleporter_pads();
        for (const Item* egg : eggs) {
            const Item* kind = egg->contents.empty() ? nullptr : world_->get(egg->contents.front());
            if (!kind || too_hard(*egg) || on_pad(pads, egg->x, egg->y, kPadReach)) continue;
            if (const std::uint16_t shape = npc_types_.word(kind->npc_number, 0x3E)) guard(shape, *egg);
        }
    }
    for (const Item* m : markers) {
        const auto spawn = std::find_if(listed.begin(), listed.end(),
                                        [&](const GuardSpawn& g) { return g.x == m->x && g.y == m->y && g.z == m->z; });
        if (spawn != listed.end() && !too_hard(*m)) guard(spawn->shape, *m);
    }
    return waiting_guards_;
}

// --probe-guards: the scan (see the top of the file). Started with a new game of the map; done() when written.
// @orig none: port-only guard scan
void Game::start_guard_probe() {
    guard_probe_ = true;
    guard_probe_wait_us_ = 0;
    guard_probe_eggs_.clear();
    guard_probe_next_ = 0;
    guard_probe_found_.clear();
    guard_probe_pending_.clear();
    guard_probe_sprites_.clear();
    guard_probe_listed_ = false;
}

// @orig none: port-only guard scan
void Game::update_guard_probe(std::uint32_t elapsed_us) {
    if (!guard_probe_ || guard_probe_done_ || !world_ || world_->current_map() < 0) return;
    guard_probe_wait_us_ += elapsed_us;
    if (!guard_probe_listed_) {
        if (guard_probe_wait_us_ < kProbeSettleUs) return;
        guard_probe_listed_ = true;
        guard_probe_wait_us_ = 0;
        world_->state.difficulty = 4;  // every guard (the game leaves out those above its difficulty)
        world_->for_each([&](const Item& i) {
            if (i.parent == 0 && !i.off_map && world_->family_of(i.shape) == World::kFamilyUnkEgg) guard_probe_eggs_.push_back(i.id);
        });
        std::fprintf(stderr, "--probe-guards: map %d, %zu trigger eggs\n", world_->current_map(), guard_probe_eggs_.size());
        return;
    }
    if (guard_probe_wait_us_ < kProbeStepUs) return;
    guard_probe_wait_us_ = 0;
    settle_guard_probe(false);
    if (guard_probe_next_ < guard_probe_eggs_.size()) {
        if (const Item* egg = world_->get(guard_probe_eggs_[guard_probe_next_])) world_->move_item(1, egg->x, egg->y, egg->z);
        ++guard_probe_next_;
        return;
    }
    // All of them: the list written.
    settle_guard_probe(true);
    const int map = world_->current_map();
    std::filesystem::create_directories(guards_file(map).parent_path());
    std::ofstream out(guards_file(map));
    out << "# Guards a trigger of map " << map << " makes, shown waiting before it fires (crusader --probe-guards, see\n";
    out << "# src/game/game_guards.cpp). guard X Y Z SHAPE: the place of the spawn marker, the NPC's shape.\n";
    std::sort(guard_probe_found_.begin(), guard_probe_found_.end());
    guard_probe_found_.erase(std::unique(guard_probe_found_.begin(), guard_probe_found_.end()), guard_probe_found_.end());
    for (const GuardSpawn& g : guard_probe_found_) out << "guard " << g.x << ' ' << g.y << ' ' << g.z << ' ' << g.shape << '\n';
    std::fprintf(stderr, "--probe-guards: %zu guards -> %s\n", guard_probe_found_.size(), guards_file(map).string().c_str());
    guard_spawns_.erase(map);
    guard_probe_done_ = true;
}

// The hooks of the scan: every guard made while the player stands on a trigger egg is that egg's, kept unless a sprite
// effect (the teleport's flash) starts where it stands about when it is made - it beams in.
// @orig none: port-only guard scan
void Game::guard_probe_hatched(ObjId) {
    if (guard_probe_) guard_probe_hatch_tick_ = kernel_->ticks();
}

// @orig none: port-only guard scan
void Game::guard_probe_created(const Item& egg, ObjId npc) {
    if (!guard_probe_ || !guard_probe_listed_ || egg.shape != kSpawnMarker) return;
    const Item* made = world_->get(npc);
    if (made) guard_probe_pending_.push_back({{egg.x, egg.y, egg.z, made->shape}, kernel_->ticks()});
}

// @orig none: port-only guard scan
void Game::guard_probe_sprite(std::uint16_t shape, std::int32_t x, std::int32_t y, std::int32_t) {
    if (guard_probe_ && guard_probe_listed_) guard_probe_sprites_.push_back({kernel_->ticks(), shape, x, y});
}

// The guards made long enough ago (or all of them): kept, or dropped as beaming in.
// @orig none: port-only guard scan
void Game::settle_guard_probe(bool all) {
    const std::uint32_t now = kernel_->ticks();
    std::erase_if(guard_probe_pending_, [&](const PendingGuard& p) {
        if (!all && now - p.tick < kProbeEffectTicks) return false;
        const bool beams = std::any_of(guard_probe_sprites_.begin(), guard_probe_sprites_.end(), [&](const ProbeSprite& s) {
            const std::uint32_t apart = s.tick > p.tick ? s.tick - p.tick : p.tick - s.tick;
            return apart <= kProbeEffectTicks && std::abs(s.x - p.spawn.x) < kEffectReach && std::abs(s.y - p.spawn.y) < kEffectReach;
        });
        if (beams) std::fprintf(stderr, "--probe-guards: %d %d beams in, left out\n", p.spawn.x, p.spawn.y);
        else guard_probe_found_.push_back(p.spawn);
        return true;
    });
}

}  // namespace crusader

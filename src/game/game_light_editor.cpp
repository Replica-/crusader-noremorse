// The light editor (--light-editor) and the lights of the maps it writes.
//
// Files: assets/lights/map_NNN.cfg (NNN the map, from the working directory), plain text, one entry a line ('#' starts a
// comment):
//   ambient A                           the map's ambient (0.5 shows the art as drawn; 0.425, 85 %, without this line)
//   light X Y Z R G B RADIUS INTENSITY [HEIGHT SIZE]  a sphere on the floor at X Y Z (usecode units), its source HEIGHT
//                                       above it; colour 0..1, radius (view pixels), intensity, bright core (SIZE)
//   cone X Y Z R G B RADIUS INTENSITY HEIGHT DIR TILT  a cone spot: its source HEIGHT above X Y Z, its spot TILT units away
//                                       in direction DIR (16ths of a turn)
//   area X Y Z R G B LEVEL              the ambient of the room of the floor at X Y Z (walls bound it), LEVEL in its colour
//   sprite SHAPE X Y Z on|off           the sprite of SHAPE at X Y Z made to light, or kept dark
// They are read once, when the lighting first needs them, and each change in the editor rewrites the map's file.
//
// The editor: the world frozen (no process runs) and all of the map shown (what the game hides too, without the black
// covers over the parts not reached yet), the lighting on, a mini map of the whole level in a corner.
//   W A S D pan, the mouse wheel zooms, Page Up / Page Down the previous / next map, a click on the mini map goes there;
//   , . the map's ambient;
//   the toolbar (left): a cone spot, a sphere spot or a room ambient dragged from it onto the map is placed there;
//   left click: select the light under the pointer; right click: its menu (height, radius, size, intensity, direction,
//   tilt, ambient level, colour, delete: the left of a row lowers, the right raises); Delete removes the one selected;
//   [ ] its radius, - = its intensity, C its colour, the arrows move it; Escape: none selected;
//   Q E: the height lights are placed at (the floor of a storey);
//   T: the sprite under the pointer as the game has it, kept dark, made to light, as it was (in turn);
//   F7: the rooms tinted (the light debug mode).
#include "game/game.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace crusader {

namespace {

constexpr std::array<float, 6> kZooms = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f};
constexpr float kPanSpeed = 700.0f;      // window pixels a second
constexpr std::int32_t kPickRadius = 14;   // view pixels round a light's marker that select it
constexpr float kHelpScale = 1.6f;         // the size of the editor's text on the window
constexpr float kMiniMapWidth = 440, kMiniMapHeight = 320;  // the mini map on the window (pixels)

struct Colour {
    const char* name;
    float r, g, b;
};
constexpr std::array kColours = {
    Colour{"WHITE", 1.0f, 1.0f, 1.0f}, Colour{"WARM WHITE", 1.0f, 0.96f, 0.86f}, Colour{"COOL WHITE", 0.85f, 0.92f, 1.0f}, Colour{"RED", 1.0f, 0.16f, 0.1f},
    Colour{"ORANGE", 1.0f, 0.6f, 0.25f},       Colour{"YELLOW", 1.0f, 0.9f, 0.35f},      Colour{"GREEN", 0.3f, 1.0f, 0.35f},
    Colour{"CYAN", 0.3f, 0.95f, 1.0f},         Colour{"BLUE", 0.3f, 0.45f, 1.0f},        Colour{"PURPLE", 0.8f, 0.4f, 1.0f},
};

std::filesystem::path lights_dir() { return std::filesystem::current_path() / "assets" / "lights"; }

std::filesystem::path map_file(int map) {
    char name[32];
    std::snprintf(name, sizeof name, "map_%03d.cfg", map);
    return lights_dir() / name;
}

// The options of a light's menu, in the order shown.
enum LightOption : int { kOptHeight, kOptRadius, kOptSize, kOptIntensity, kOptDirection, kOptTilt, kOptLevel, kOptColour, kOptDelete };

// A light's marker above its floor: a sphere's and a cone's at their source, an ambient area's just above the floor.
template <class Light>
float marker_height(const Light& l) { return l.type == 2 /* an ambient area */ ? 16.0f : l.height; }

int nearest_colour(float r, float g, float b) {
    int best = 0;
    float best_d = 1e9f;
    for (std::size_t i = 0; i < kColours.size(); ++i) {
        const float d = std::abs(kColours[i].r - r) + std::abs(kColours[i].g - g) + std::abs(kColours[i].b - b);
        if (d < best_d) best_d = d, best = static_cast<int>(i);
    }
    return best;
}

}  // namespace

// The files of assets/lights, once.
// @orig none: port-only light editor
void Game::load_map_lights() {
    if (map_lights_loaded_) return;
    map_lights_loaded_ = true;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(lights_dir(), error)) {
        int map = 0;
        if (std::sscanf(entry.path().filename().string().c_str(), "map_%d.cfg", &map) != 1) continue;
        std::ifstream in(entry.path());
        MapLights& lights = map_lights_[map];
        for (std::string line; std::getline(in, line);) {
            if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
            std::istringstream words(line);
            std::string kind;
            if (!(words >> kind)) continue;
            if (kind == "ambient") {
                words >> lights.ambient;
            } else if (kind == "light") {
                PlacedLight l;
                if (words >> l.x >> l.y >> l.z >> l.r >> l.g >> l.b >> l.radius >> l.intensity) {
                    float height = 0, size = 0;  // (optional: files from before have neither)
                    if (words >> height && height >= 1) l.height = height;
                    if (words >> size) l.size = size;
                    lights.lights.push_back(l);
                }
            } else if (kind == "cone") {
                PlacedLight l;
                l.type = kConeLight;
                if (words >> l.x >> l.y >> l.z >> l.r >> l.g >> l.b >> l.radius >> l.intensity >> l.height >> l.dir >> l.tilt) {
                    lights.lights.push_back(l);
                }
            } else if (kind == "area") {
                PlacedLight l;
                l.type = kAreaAmbient;
                if (words >> l.x >> l.y >> l.z >> l.r >> l.g >> l.b >> l.intensity) lights.lights.push_back(l);
            } else if (kind == "sprite") {
                std::string shape, state;
                SpriteLight s;
                if (words >> shape >> s.x >> s.y >> s.z >> state) {
                    s.shape = static_cast<std::uint16_t>(std::stoi(shape, nullptr, 0));
                    s.on = state == "on";
                    lights.sprites.push_back(s);
                }
            }
        }
    }
}

// The file of `map`, written again (removed when the map has nothing).
// @orig none: port-only light editor
void Game::save_map_lights(int map) const {
    const auto found = map_lights_.find(map);
    const std::filesystem::path path = map_file(map);
    std::error_code error;
    if (found == map_lights_.end() || (found->second.lights.empty() && found->second.sprites.empty() && found->second.ambient < 0)) {
        std::filesystem::remove(path, error);
        return;
    }
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream out(path);
    out << "# Crusader lights of map " << map << " (the light editor, --light-editor)\n";
    out << "# ambient A / light (a sphere) X Y Z R G B RADIUS INTENSITY HEIGHT SIZE\n";
    out << "# cone X Y Z R G B RADIUS INTENSITY HEIGHT DIR TILT / area (a room's ambient) X Y Z R G B LEVEL\n";
    out << "# sprite SHAPE X Y Z on|off\n";
    char line[160];
    if (found->second.ambient >= 0) {
        std::snprintf(line, sizeof line, "ambient %.3f\n", static_cast<double>(found->second.ambient));
        out << line;
    }
    for (const PlacedLight& l : found->second.lights) {
        const double r = l.r, g = l.g, b = l.b;
        if (l.type == kConeLight) {
            std::snprintf(line, sizeof line, "cone %d %d %d %.2f %.2f %.2f %.0f %.2f %.0f %d %.0f\n", l.x, l.y, l.z, r, g, b,
                          static_cast<double>(l.radius), static_cast<double>(l.intensity), static_cast<double>(l.height), l.dir,
                          static_cast<double>(l.tilt));
        } else if (l.type == kAreaAmbient) {
            std::snprintf(line, sizeof line, "area %d %d %d %.2f %.2f %.2f %.3f\n", l.x, l.y, l.z, r, g, b, static_cast<double>(l.intensity));
        } else {
            std::snprintf(line, sizeof line, "light %d %d %d %.2f %.2f %.2f %.0f %.2f %.0f %.2f\n", l.x, l.y, l.z, r, g, b,
                          static_cast<double>(l.radius), static_cast<double>(l.intensity), static_cast<double>(l.height),
                          static_cast<double>(l.size));
        }
        out << line;
    }
    for (const SpriteLight& s : found->second.sprites) {
        std::snprintf(line, sizeof line, "sprite 0x%03X %d %d %d %s\n", s.shape, s.x, s.y, s.z, s.on ? "on" : "off");
        out << line;
    }
}

// @orig none: port-only light editor
Game::MapLights* Game::current_map_lights() {
    if (!world_) return nullptr;
    load_map_lights();
    return &map_lights_[world_->current_map()];
}

// @orig none: port-only light editor
const Game::SpriteLight* Game::sprite_light(const Item& item) {
    MapLights* lights = current_map_lights();
    if (!lights) return nullptr;
    for (const SpriteLight& s : lights->sprites) {
        if (s.shape == item.shape && s.x == item.x && s.y == item.y && s.z == item.z) return &s;
    }
    return nullptr;
}

// @orig none: port-only light editor
void Game::start_light_editor() {
    editor_ = true;
    config_.lighting = true;
    config_.follow_player = false;  // (not saved: the options screen is not opened)
    if (world_) {
        world_->set_follow_player(false);
        if (const Item* npc = world_->get(world_->state.controlled_npc)) editor_z_ = world_->location(*npc).z;
    }
    load_map_lights();
    editor_status_ = "SAVES TO ASSETS/LIGHTS";
}

// @orig none: port-only light editor
void Game::editor_wheel(int steps) {
    int i = 0;
    while (i + 1 < static_cast<int>(kZooms.size()) && kZooms[static_cast<std::size_t>(i)] < editor_zoom_ - 0.01f) ++i;
    i = std::clamp(i + steps, 0, static_cast<int>(kZooms.size()) - 1);
    editor_zoom_ = kZooms[static_cast<std::size_t>(i)];
}

// Whether the world is frozen for the editor yet: the game runs until a map is loaded and some seconds after (its
// start-up scripts load the map and fade the screen in), then stops.
// @orig none: port-only light editor
bool Game::editor_frozen(std::uint32_t elapsed_us) {
    constexpr float kWarmup = 3.0f;
    if (editor_warmup_ >= kWarmup) return true;
    if (world_ && world_->current_map() > 0) editor_warmup_ += static_cast<float>(elapsed_us) / 1e6f;
    if (editor_warmup_ >= kWarmup) {  // frozen from now: lights placed at the player's floor to begin with
        if (const Item* npc = world_->get(world_->state.controlled_npc)) editor_z_ = world_->location(*npc).z;
        world_->set_follow_player(false);
        // All of the map: its glob eggs expanded (the game builds them only near the camera and drops them behind it).
        world_->freeze_fast_area(true);
        world_->expand_glob_eggs(globs_);
        editor_minimap_dirty_ = true;
    }
    return false;
}

// The camera moved by the keys held: on the screen (W up, S down, A left, D right), the same speed at any zoom.
// @orig none: port-only light editor
void Game::update_editor(float seconds) {
    if (!world_) return;
    world_->set_follow_player(false);
    const float sx = static_cast<float>(editor_pan_[3] - editor_pan_[1]), sy = static_cast<float>(editor_pan_[2] - editor_pan_[0]);
    if (sx == 0 && sy == 0) return;
    const float step = kPanSpeed / editor_zoom_ * std::min(seconds, 0.05f);
    // A screen move (dx, dy) at a fixed height: x - y = 2 dx, x + y = 4 dy.
    const float dx = sx * step, dy = sy * step;
    const GameState& s = world_->state;
    world_->set_camera(s.camera_x + static_cast<std::int32_t>(std::lround(dx + 2 * dy)),
                       s.camera_y + static_cast<std::int32_t>(std::lround(2 * dy - dx)), s.camera_z);
}

// The next (or previous) map with items, the camera in the middle of them.
// @orig none: port-only light editor
void Game::editor_goto_map(int step) {
    if (!world_) return;
    const int count = static_cast<int>(fixed_.count());
    int map = world_->current_map();
    for (int tries = 0; tries < count; ++tries) {
        map += step;
        if (map < 1) map = count - 1;
        if (map >= count) map = 1;
        if (!read_fixed_map(fixed_, static_cast<std::size_t>(map)).empty()) break;
    }
    const GameState& s = world_->state;
    world_->move_camera_to(s.camera_x, s.camera_y, s.camera_z, map);
    world_->expand_glob_eggs(globs_);  // (all of it: the fast area is frozen)
    editor_minimap_dirty_ = true;
    // The camera on the middle item (the median: the scripts' spare items lie far off the level).
    std::vector<std::int32_t> xs, ys;
    world_->for_each([&](const Item& item) {
        if (item.parent != 0 || item.off_map || is_npc_id(item.id)) return;
        xs.push_back(item.x), ys.push_back(item.y);
    });
    if (!xs.empty()) {
        std::nth_element(xs.begin(), xs.begin() + static_cast<std::ptrdiff_t>(xs.size() / 2), xs.end());
        std::nth_element(ys.begin(), ys.begin() + static_cast<std::ptrdiff_t>(ys.size() / 2), ys.end());
        world_->set_camera(xs[xs.size() / 2], ys[ys.size() / 2], 0);
    }
    editor_selected_ = -1;
    editor_z_ = 0;
    editor_status_ = "MAP " + std::to_string(world_->current_map());
}

// The floor at the placing height under (x, y) of the view (the view's projection undone).
// @orig none: port-only light editor
World::Point Game::editor_point(int screen_x, int screen_y) const {
    const GameState& s = world_->state;
    const std::int32_t a = 2 * (screen_x + s.view_left), b = 4 * (screen_y + s.view_top + editor_z_);  // x - y, x + y
    return World::Point{(a + b) / 2, (b - a) / 2, editor_z_};
}

// The placed light whose marker is nearest (x, y) of the view, -1 for none near.
// @orig none: port-only light editor
int Game::editor_light_at(int screen_x, int screen_y) const {
    const auto found = map_lights_.find(world_ ? world_->current_map() : -1);
    if (found == map_lights_.end()) return -1;
    const GameState& s = world_->state;
    int best = -1;
    float best_d = static_cast<float>(kPickRadius);
    for (std::size_t i = 0; i < found->second.lights.size(); ++i) {
        const PlacedLight& l = found->second.lights[i];
        const float sx = static_cast<float>((l.x - l.y) / 2 - s.view_left);
        const float sy = static_cast<float>((l.x + l.y) / 4 - (l.z + marker_height(l)) - s.view_top);
        const float d = std::hypot(sx - static_cast<float>(screen_x), sy - static_cast<float>(screen_y));
        if (d <= best_d) best_d = d, best = static_cast<int>(i);
    }
    return best;
}

// @orig none: port-only light editor
void Game::editor_mouse(int button, bool pressed) {
    if (!world_) return;
    MapLights* lights = current_map_lights();
    if (!lights) return;
    const int map = world_->current_map();
    // Releasing a light dragged from the toolbar: placed there (not on the toolbar, the menu or the mini map).
    if (button == 1 && !pressed) {
        if (editor_drag_ < 0) return;
        const int type = editor_drag_;
        editor_drag_ = -1;
        const LightDebugOverlay::MiniMap& m = editor_mini_panel_;
        const bool on_minimap = m.polys && pointer_x_ >= m.x && pointer_x_ <= m.x + m.w && pointer_y_ >= m.y && pointer_y_ <= m.y + m.h;
        if (editor_button_at(pointer_x_, pointer_y_) >= 0 || on_minimap) {
            editor_status_ = "NOT PLACED";
            return;
        }
        editor_place(type, pointer_x_, pointer_y_);
        return;
    }
    if (!pressed) return;
    // The toolbar and the menu.
    if (const int i = editor_button_at(pointer_x_, pointer_y_); i >= 0) {
        if (button != 1) return;
        const EditorButton& b = editor_buttons_[static_cast<std::size_t>(i)];
        if (b.action >= 100) {
            editor_drag_ = b.action - 100;
            editor_status_ = "DROP IT ON THE MAP";
            return;
        }
        if (editor_menu_ < 0 || editor_menu_ >= static_cast<int>(lights->lights.size())) return;
        // A row: its left part lowers, its right part raises (the middle: the next colour; delete anywhere).
        const float at = (static_cast<float>(pointer_x_) - b.x) / b.w;
        const int step = at < 0.35f ? -1 : at > 0.65f ? 1 : (b.action == kOptColour ? 1 : 0);
        if (b.action == kOptDelete) {
            lights->lights.erase(lights->lights.begin() + editor_menu_);
            editor_selected_ = editor_menu_ = -1;
            editor_status_ = "LIGHT REMOVED";
        } else if (step != 0) {
            editor_adjust(lights->lights[static_cast<std::size_t>(editor_menu_)], b.action, step);
        }
        save_map_lights(map);
        return;
    }
    // The mini map: the camera there.
    if (const LightDebugOverlay::MiniMap& m = editor_mini_panel_;
        m.polys && pointer_x_ >= m.x && pointer_x_ <= m.x + m.w && pointer_y_ >= m.y && pointer_y_ <= m.y + m.h) {
        if (button == 1) {
            const float mx = (static_cast<float>(pointer_x_) - m.ox) / m.scale, my = (static_cast<float>(pointer_y_) - m.oy) / m.scale;
            world_->set_camera(static_cast<std::int32_t>(mx + 2 * my), static_cast<std::int32_t>(2 * my - mx), world_->state.camera_z);
            editor_status_ = "MOVED (MINI MAP)";
        }
        return;
    }
    // A light: selected (left), its menu opened (right); elsewhere, none.
    const int at = editor_light_at(pointer_x_, pointer_y_);
    editor_selected_ = at;
    editor_menu_ = button == 2 ? at : -1;
    editor_status_ = at < 0 ? "" : button == 2 ? "MENU: LEFT/RIGHT OF A ROW LOWERS/RAISES" : "LIGHT SELECTED";
}

// The toolbar or menu button at (x, y) of the view, -1 for none.
// @orig none: port-only light editor
int Game::editor_button_at(int x, int y) const {
    for (std::size_t i = 0; i < editor_buttons_.size(); ++i) {
        const EditorButton& b = editor_buttons_[i];
        if (static_cast<float>(x) >= b.x && static_cast<float>(x) <= b.x + b.w && static_cast<float>(y) >= b.y && static_cast<float>(y) <= b.y + b.h) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// A new light of `type` on the floor (at the placing height) under (x, y) of the view, selected, its menu open.
// @orig none: port-only light editor
void Game::editor_place(int type, int screen_x, int screen_y) {
    MapLights* lights = current_map_lights();
    if (!lights) return;
    PlacedLight l;
    l.type = type;
    if (type == kConeLight) {
        l.height = 120, l.radius = 90, l.intensity = 0.9f;
    } else if (type == kAreaAmbient) {
        l.r = l.g = l.b = 1.0f, l.intensity = 0.5f;
    }
    const World::Point p = editor_point(screen_x, screen_y);
    l.x = p.x, l.y = p.y, l.z = p.z;
    lights->lights.push_back(l);
    editor_selected_ = editor_menu_ = static_cast<int>(lights->lights.size()) - 1;
    editor_status_ = type == kConeLight ? "CONE PLACED" : type == kAreaAmbient ? "ROOM AMBIENT PLACED" : "SPHERE PLACED";
    save_map_lights(world_->current_map());
}

// One option of a light changed by a step (-1 or 1).
// @orig none: port-only light editor
void Game::editor_adjust(PlacedLight& l, int option, int step) {
    const float s = static_cast<float>(step);
    char what[64];
    switch (option) {
    case kOptHeight: l.height = std::clamp(l.height + 8 * s, 0.0f, 240.0f), std::snprintf(what, sizeof what, "HEIGHT %.0f", static_cast<double>(l.height)); break;
    case kOptRadius: l.radius = std::clamp(l.radius + 10 * s, 20.0f, 600.0f), std::snprintf(what, sizeof what, "RADIUS %.0f", static_cast<double>(l.radius)); break;
    case kOptSize: l.size = std::clamp(l.size + 0.1f * s, 0.0f, 0.9f), std::snprintf(what, sizeof what, "SIZE %.0f %%", static_cast<double>(l.size * 100)); break;
    case kOptIntensity: l.intensity = std::clamp(l.intensity + 0.05f * s, 0.05f, 1.5f), std::snprintf(what, sizeof what, "INTENSITY %.2f", static_cast<double>(l.intensity)); break;
    case kOptLevel: l.intensity = std::clamp(l.intensity + 0.025f * s, 0.025f, 0.75f), std::snprintf(what, sizeof what, "AMBIENT %.0f %%", static_cast<double>(l.intensity * 200)); break;
    case kOptDirection: l.dir = (l.dir + step + 16) % 16, std::snprintf(what, sizeof what, "DIRECTION %d", l.dir); break;
    case kOptTilt: l.tilt = std::clamp(l.tilt + 16 * s, 0.0f, 400.0f), std::snprintf(what, sizeof what, "TILT %.0f", static_cast<double>(l.tilt)); break;
    case kOptColour: {
        const int n = static_cast<int>(kColours.size());
        const Colour& c = kColours[static_cast<std::size_t>((nearest_colour(l.r, l.g, l.b) + step + n) % n)];
        l.r = c.r, l.g = c.g, l.b = c.b;
        std::snprintf(what, sizeof what, "COLOUR %s", c.name);
        break;
    }
    default: return;
    }
    editor_status_ = what;
}

// The editor's keys; true when the key is the editor's (the game gets none while it is on).
// @orig none: port-only light editor
bool Game::editor_key(std::uint8_t scancode, bool extended, bool pressed) {
    if (!editor_ || !world_) return false;
    // Panning: held.
    if (!extended) {
        const int pan = scancode == 0x11 ? 0 : scancode == 0x1E ? 1 : scancode == 0x1F ? 2 : scancode == 0x20 ? 3 : -1;
        if (pan >= 0) {
            editor_pan_[static_cast<std::size_t>(pan)] = pressed;
            return true;
        }
    }
    if (!pressed) return true;
    MapLights* lights = current_map_lights();
    const int map = world_->current_map();
    PlacedLight* selected = lights && editor_selected_ >= 0 && editor_selected_ < static_cast<int>(lights->lights.size())
                                ? &lights->lights[static_cast<std::size_t>(editor_selected_)]
                                : nullptr;
    const auto changed = [&](const std::string& what) {
        editor_status_ = what;
        save_map_lights(map);
    };
    if (extended) {
        switch (scancode) {
        case 0x49: editor_goto_map(-1); break;  // Page Up
        case 0x51: editor_goto_map(1); break;   // Page Down
        case 0x53:                              // Delete
            if (selected) {
                lights->lights.erase(lights->lights.begin() + editor_selected_);
                editor_selected_ = -1;
                changed("LIGHT REMOVED");
            }
            break;
        case 0x48: case 0x50: case 0x4B: case 0x4D:  // the arrows: the light moved on the screen
            if (selected) {
                const int dx = scancode == 0x4B ? -8 : scancode == 0x4D ? 8 : 0, dy = scancode == 0x48 ? -4 : scancode == 0x50 ? 4 : 0;
                selected->x += dx + 2 * dy, selected->y += 2 * dy - dx;
                changed("LIGHT MOVED");
            }
            break;
        default: break;
        }
        return true;
    }
    switch (scancode) {
    case 0x01: editor_selected_ = -1; break;  // Escape
    case 0x10: editor_z_ = std::max(0, editor_z_ - 8), editor_status_ = "PLACING HEIGHT " + std::to_string(editor_z_); break;    // Q
    case 0x12: editor_z_ = std::min(240, editor_z_ + 8), editor_status_ = "PLACING HEIGHT " + std::to_string(editor_z_); break;  // E
    case 0x41: light_debug_ = !light_debug_; break;  // F7: the rooms tinted
    case 0x33: case 0x34: {  // , .: the map's ambient, 5 % at a time
        if (!lights) break;
        const float now = lights->ambient >= 0 ? lights->ambient : SceneLighting{}.dark_r;
        lights->ambient = std::clamp(now + (scancode == 0x34 ? 0.025f : -0.025f), 0.025f, 0.75f);
        char what[48];
        std::snprintf(what, sizeof what, "AMBIENT %.0f %%", static_cast<double>(lights->ambient * 200));
        changed(what);
        break;
    }
    case 0x14: {  // T: the sprite under the pointer
        const auto under = pick(editor_list_, pointer_x_, pointer_y_);
        if (under.empty() || !lights) break;
        const Item& item = *editor_list_[under.back()].item;
        auto it = std::find_if(lights->sprites.begin(), lights->sprites.end(), [&](const SpriteLight& s) {
            return s.shape == item.shape && s.x == item.x && s.y == item.y && s.z == item.z;
        });
        std::string state;
        if (it == lights->sprites.end()) {
            const bool lit = room_light_level(item) > 0;  // (as the game has it: no word on it yet)
            lights->sprites.push_back({item.shape, item.x, item.y, item.z, !lit});
            state = lit ? "KEPT DARK" : "MADE TO LIGHT";
        } else if (!it->on) {
            it->on = true;
            state = "MADE TO LIGHT";
        } else {
            lights->sprites.erase(it);
            state = "AS THE GAME HAS IT";
        }
        std::string name = object_name(item.shape);
        if (name.empty()) name = "SPRITE";
        char what[96];
        std::snprintf(what, sizeof what, "%s 0x%X: %s", name.c_str(), item.shape, state.c_str());
        changed(what);
        break;
    }
    default:
        if (!selected) break;
        switch (scancode) {
        case 0x1A: selected->radius = std::max(20.0f, selected->radius - 10), changed("RADIUS"); break;         // [
        case 0x1B: selected->radius = std::min(600.0f, selected->radius + 10), changed("RADIUS"); break;        // ]
        case 0x0C: selected->intensity = std::max(0.05f, selected->intensity - 0.05f), changed("INTENSITY"); break;  // -
        case 0x0D: selected->intensity = std::min(1.5f, selected->intensity + 0.05f), changed("INTENSITY"); break;   // =
        case 0x2E: {  // C
            const Colour& c = kColours[static_cast<std::size_t>(nearest_colour(selected->r, selected->g, selected->b) + 1) % kColours.size()];
            selected->r = c.r, selected->g = c.g, selected->b = c.b;
            changed(std::string("COLOUR ") + c.name);
            break;
        }
        default: break;
        }
    }
    return true;
}

// Whether `frame` is a black cover: every pixel black in the game palette (GAMEPAL, not the faded screen's).
// @orig none: port-only light editor
bool Game::is_black_cover(const ShapeFrame& frame) {
    const auto found = black_covers_.find(&frame);
    if (found != black_covers_.end()) return found->second;
    bool black = false;
    if (world_ && frame.width > 0 && frame.height > 0) {
        Surface8 scratch(frame.width, frame.height);
        scratch.clear(255);
        frame.draw(scratch, frame.xoff, frame.yoff);
        int pixels = 0, dark = 0;
        for (const std::uint8_t p : scratch.pixels) {
            if (p == 255) continue;  // (the clear colour)
            ++pixels;
            const Rgb& c = world_->state.game_palette.colors[p];
            if (c.r + c.g + c.b < 12) ++dark;
        }
        black = pixels > 0 && dark == pixels;
    }
    return black_covers_.emplace(&frame, black).first->second;
}

// The mini map: the floors (blue grey) and walls (light) of the whole level, as the mission map draws them (map pixels:
// ((x - y) / 2, (x + y) / 4) of usecode units, without the height), black covers and markers left out. Its bounds leave out
// the few items far off the level (the scripts' spare ones): the 1st to the 99th hundredth of the floors' places.
// @orig none: port-only light editor
void Game::build_editor_minimap() {
    editor_minimap_.clear();
    editor_minimap_map_ = world_ ? world_->current_map() : -1;
    editor_minimap_dirty_ = false;
    if (!world_) return;
    std::vector<MapPoly> floors, walls;
    std::vector<float> xs, ys;
    world_->for_each([&](const Item& item) {
        if (item.off_map || item.parent != 0 || (item.flags & kItemContained) || is_npc_id(item.id)) return;
        const ShapeInfo* info = world_->shape_info(item.shape);
        if (!info || info->is(ShapeInfo::kEditor)) return;
        const Shape* shape = shapes_.get(item.shape);
        if (shape && item.frame < shape->frames().size() && is_black_cover(shape->frames()[item.frame])) return;
        const bool mirrored = item.flags & kItemFlipped;
        const float x = static_cast<float>(item.x), y = static_cast<float>(item.y);
        const float x_left = x - static_cast<float>((mirrored ? info->footpad_y : info->footpad_x) * 16);
        const float y_far = y - static_cast<float>((mirrored ? info->footpad_x : info->footpad_y) * 16);
        if (x_left >= x && y_far >= y) return;
        MapPoly p;
        const float px[4] = {x_left, x, x, x_left}, py[4] = {y_far, y_far, y, y};
        for (int i = 0; i < 4; ++i) p.x[i] = (px[i] - py[i]) * 0.5f, p.y[i] = (px[i] + py[i]) * 0.25f;
        if (info->is(ShapeInfo::kLand) && info->footpad_z <= 1) {
            p.r = 70, p.g = 98, p.b = 120, p.a = 255;
            floors.push_back(p);
            xs.push_back((p.x[0] + p.x[2]) * 0.5f), ys.push_back((p.y[0] + p.y[2]) * 0.5f);
        } else if (is_wall(item.shape)) {
            // (thickened: a wall is a pixel or less across at the mini map's scale)
            const float t = 40.0f, qx[4] = {x_left - t, x + t, x + t, x_left - t}, qy[4] = {y_far - t, y_far - t, y + t, y + t};
            for (int i = 0; i < 4; ++i) p.x[i] = (qx[i] - qy[i]) * 0.5f, p.y[i] = (qx[i] + qy[i]) * 0.25f;
            p.r = 225, p.g = 225, p.b = 235, p.a = 255;
            walls.push_back(p);
        }
    });
    editor_minimap_ = std::move(floors);
    editor_minimap_.insert(editor_minimap_.end(), walls.begin(), walls.end());
    if (xs.empty()) return;
    const auto at = [](std::vector<float>& v, float q) {
        const auto k = static_cast<std::ptrdiff_t>(q * static_cast<float>(v.size() - 1));
        std::nth_element(v.begin(), v.begin() + k, v.end());
        return v[static_cast<std::size_t>(k)];
    };
    mini_min_x_ = at(xs, 0.01f) - 64, mini_max_x_ = at(xs, 0.99f) + 64;
    mini_min_y_ = at(ys, 0.01f) - 32, mini_max_y_ = at(ys, 0.99f) + 32;
}

// The editor's markers and lines over the light debug overlay (or alone): the placed lights (rings, the one selected
// in white), the sprites it has a word on (crosses: white made to light, magenta kept dark), the keys.
// @orig none: port-only light editor
void Game::build_editor_overlay() {
    if (!editor_ || !world_) return;
    LightDebugOverlay& o = light_debug_overlay_;
    if (!o.on) {
        o = LightDebugOverlay{};
        o.on = true;
    }
    MapLights* lights = current_map_lights();
    const GameState& s = world_->state;
    const auto project = [&](std::int32_t x, std::int32_t y, std::int32_t z, float& sx, float& sy) {
        sx = static_cast<float>((x - y) / 2 - s.view_left);
        sy = static_cast<float>((x + y) / 4 - z - s.view_top);
    };
    std::size_t n_lights = 0, n_sprites = 0;
    if (lights) {
        n_lights = lights->lights.size(), n_sprites = lights->sprites.size();
        for (std::size_t i = 0; i < lights->lights.size(); ++i) {
            const PlacedLight& l = lights->lights[i];
            float sx, sy;
            project(l.x, l.y, l.z + static_cast<std::int32_t>(marker_height(l)), sx, sy);
            o.marks.push_back({sx, sy, false, static_cast<int>(i) == editor_selected_, static_cast<std::uint8_t>(l.type == kAreaAmbient ? 1 : l.type == kConeLight ? 2 : 0)});
        }
        for (const SpriteLight& sp : lights->sprites) {
            float sx, sy;
            project(sp.x - 8, sp.y - 8, sp.z + 8, sx, sy);
            o.marks.push_back({sx, sy, true, sp.on});
        }
    }
    char line[256];
    std::snprintf(line, sizeof line, "LIGHT EDITOR - MAP %d - AMBIENT %.0f %% - %zu LIGHTS, %zu SPRITES - HEIGHT %d - ZOOM %.2f - %s",
                  world_->current_map(), static_cast<double>(lighting_.dark_r * 200), n_lights, n_sprites, editor_z_,
                  static_cast<double>(editor_zoom_), editor_status_.c_str());
    o.text = line;
    o.lines.clear();
    o.text_scale = kHelpScale / editor_zoom_;  // (large, and the same size at any zoom)
    // The mini map, top right under the status line, the same size on the window at any zoom.
    if (editor_minimap_dirty_ || editor_minimap_map_ != world_->current_map()) build_editor_minimap();
    if (!editor_minimap_.empty()) {
        LightDebugOverlay::MiniMap& m = o.minimap;
        const float k = 1.0f / editor_zoom_;
        const float view_w = static_cast<float>(s.screen_right + 1);
        m.polys = &editor_minimap_;
        m.w = kMiniMapWidth * k, m.h = kMiniMapHeight * k;
        m.x = view_w - m.w - 12 * k, m.y = 40 * k * kHelpScale;
        const float bw = mini_max_x_ - mini_min_x_, bh = mini_max_y_ - mini_min_y_;
        m.scale = std::min(m.w / bw, m.h / bh);
        m.ox = m.x + (m.w - bw * m.scale) * 0.5f - mini_min_x_ * m.scale;
        m.oy = m.y + (m.h - bh * m.scale) * 0.5f - mini_min_y_ * m.scale;
        const int corners[4][2] = {{0, 0}, {s.screen_right + 1, 0}, {s.screen_right + 1, s.screen_bottom + 1}, {0, s.screen_bottom + 1}};
        for (int i = 0; i < 4; ++i) {
            const World::Point p = editor_point(corners[i][0], corners[i][1]);
            m.view_x[i] = static_cast<float>(p.x - p.y) * 0.5f, m.view_y[i] = static_cast<float>(p.x + p.y) * 0.25f;
        }
        editor_mini_panel_ = m;
    }
    static constexpr const char* kTypeNames[] = {"SPHERE", "CONE", "ROOM AMBIENT"};
    if (lights && editor_selected_ >= 0 && editor_selected_ < static_cast<int>(lights->lights.size())) {
        const PlacedLight& l = lights->lights[static_cast<std::size_t>(editor_selected_)];
        std::snprintf(line, sizeof line, "SELECTED: %s, %s - RIGHT CLICK IT FOR ITS OPTIONS", kTypeNames[l.type],
                      kColours[static_cast<std::size_t>(nearest_colour(l.r, l.g, l.b))].name);
        o.lines.push_back(line);
    }
    o.lines.push_back("WASD PAN   WHEEL ZOOM   PAGE UP/DOWN MAP");
    o.lines.push_back("Q/E PLACING HEIGHT   , . MAP AMBIENT   F7 ROOMS");
    o.lines.push_back("DRAG A LIGHT FROM THE TOOLBAR ONTO THE MAP");
    o.lines.push_back("LEFT CLICK SELECT   RIGHT CLICK OPTIONS");
    o.lines.push_back("SELECTED: DELETE REMOVE   ARROWS MOVE");
    o.lines.push_back("T SPRITE: GAME'S / DARK / LIT");

    // The toolbar (left, under the help) and the menu of a light (right click), on the window at any zoom.
    editor_buttons_.clear();
    const float k = 1.0f / editor_zoom_, ts = 1.3f * k;
    const auto box = [&](float x, float y, float w, float h, bool hot, const std::string& text, int action) {
        LightDebugOverlay::Box b{x, y, w, h, {14, 18, 26, 225}, {120, 135, 160, 255}, {225, 235, 255}, text, ts, {}};
        if (hot) b.fill[0] = 60, b.fill[1] = 52, b.fill[2] = 18, b.edge[0] = 255, b.edge[1] = 220, b.edge[2] = 60, b.ink[2] = 160;
        o.boxes.push_back(b);
        if (action >= 0) editor_buttons_.push_back({x, y, w, h, action});
    };
    o.boxes.clear();
    {
        const float x = 10 * k, w = 250 * k, h = 34 * k;
        float y = (34.0f + 14.0f * static_cast<float>(o.lines.size() + 1)) * kHelpScale * k;
        box(x, y, w, 26 * k, false, "LIGHTS (DRAG ONE)", -1);
        y += 30 * k;
        static constexpr const char* kTools[] = {"SPOT: CONE", "SPOT: SPHERE", "ROOM AMBIENT"};
        static constexpr int kToolTypes[] = {kConeLight, kSphereLight, kAreaAmbient};
        for (int i = 0; i < 3; ++i, y += h + 6 * k) box(x, y, w, h, editor_drag_ == kToolTypes[i], kTools[i], 100 + kToolTypes[i]);
    }
    if (lights && editor_menu_ >= 0 && editor_menu_ < static_cast<int>(lights->lights.size())) {
        const PlacedLight& l = lights->lights[static_cast<std::size_t>(editor_menu_)];
        std::vector<int> rows;
        if (l.type == kSphereLight) rows = {kOptHeight, kOptRadius, kOptSize, kOptIntensity, kOptColour, kOptDelete};
        else if (l.type == kConeLight) rows = {kOptHeight, kOptRadius, kOptIntensity, kOptDirection, kOptTilt, kOptColour, kOptDelete};
        else rows = {kOptLevel, kOptColour, kOptDelete};
        float sx, sy;
        project(l.x, l.y, l.z + static_cast<std::int32_t>(marker_height(l)), sx, sy);
        const float w = 300 * k, h = 30 * k, total = h * static_cast<float>(rows.size() + 1);
        const float view_w = static_cast<float>(s.screen_right + 1), view_h = static_cast<float>(s.screen_bottom + 1);
        const float x = std::clamp(sx + 24 * k, 4 * k, view_w - w - 4 * k), y0 = std::clamp(sy - h, 4 * k, view_h - total - 4 * k);
        box(x, y0, w, h, false, std::string(kTypeNames[l.type]) + " - OPTIONS", -1);
        float y = y0 + h;
        for (const int opt : rows) {
            char text[64];
            switch (opt) {
            case kOptHeight: std::snprintf(text, sizeof text, "<   HEIGHT %.0f", static_cast<double>(l.height)); break;
            case kOptRadius: std::snprintf(text, sizeof text, "<   RADIUS %.0f", static_cast<double>(l.radius)); break;
            case kOptSize: std::snprintf(text, sizeof text, "<   SIZE %.0f %%", static_cast<double>(l.size * 100)); break;
            case kOptIntensity: std::snprintf(text, sizeof text, "<   INTENSITY %.2f", static_cast<double>(l.intensity)); break;
            case kOptDirection: std::snprintf(text, sizeof text, "<   DIRECTION %d / 16", l.dir); break;
            case kOptTilt: std::snprintf(text, sizeof text, "<   TILT %.0f", static_cast<double>(l.tilt)); break;
            case kOptLevel: std::snprintf(text, sizeof text, "<   AMBIENT %.0f %%", static_cast<double>(l.intensity * 200)); break;
            case kOptColour:
                std::snprintf(text, sizeof text, "<   COLOUR %s", kColours[static_cast<std::size_t>(nearest_colour(l.r, l.g, l.b))].name);
                break;
            default: std::snprintf(text, sizeof text, "    DELETE"); break;
            }
            box(x, y, w, h, false, text, opt);
            if (opt != kOptDelete) o.boxes.back().right = ">";  // (the right part of a row raises)
            y += h;
        }
    }
    // A light dragged from the toolbar: its marker under the pointer.
    if (editor_drag_ >= 0) {
        o.marks.push_back({static_cast<float>(pointer_x_), static_cast<float>(pointer_y_), false, true,
                           static_cast<std::uint8_t>(editor_drag_ == kAreaAmbient ? 1 : editor_drag_ == kConeLight ? 2 : 0)});
    }
}

// @orig none: port-only test hook
void Game::demo_light_editor(double fx, double fy) {
    if (!editor_ || !world_) return;
    if (fy > 3.5) {  // (a fourth: the map's ambient raised four steps)
        for (int i = 0; i < 4; ++i) editor_key(0x34, false, true);
        return;
    }
    if (fy > 2.5) {  // (a third: zoomed out twice, on this map)
        editor_wheel(-2);
        return;
    }
    if (fy > 1.5) {  // (the other test: the next map, zoomed out twice)
        editor_goto_map(1);
        editor_wheel(-2);
        return;
    }
    // The toolbar used as the mouse would: a cone dragged to the middle of the view (its menu opens), a sphere and a room
    // ambient beside it; the cone then leant and coloured; the sprite at (fx, fy) of the view toggled (T).
    const GameState& s = world_->state;
    const int w = s.screen_right + 1, h = s.screen_bottom + 1;
    const auto drag = [&](int type, int x, int y) {
        for (const EditorButton& b : editor_buttons_) {
            if (b.action != 100 + type) continue;
            pointer_x_ = static_cast<int>(b.x + b.w / 2), pointer_y_ = static_cast<int>(b.y + b.h / 2);
            editor_mouse(1, true);
            pointer_x_ = x, pointer_y_ = y;
            editor_mouse(1, false);
            return;
        }
    };
    drag(kSphereLight, w * 3 / 10, h * 6 / 10);
    drag(kAreaAmbient, w * 7 / 10, h * 7 / 10);
    drag(kConeLight, w / 2, h / 2);
    if (MapLights* lights = current_map_lights(); lights && editor_selected_ >= 0) {
        PlacedLight& cone = lights->lights[static_cast<std::size_t>(editor_selected_)];
        for (int i = 0; i < 4; ++i) editor_adjust(cone, kOptTilt, 1);
        for (int i = 0; i < 3; ++i) editor_adjust(cone, kOptDirection, 1);
        editor_adjust(cone, kOptColour, 1);
        save_map_lights(world_->current_map());
    }
    pointer_x_ = static_cast<int>(fx * w), pointer_y_ = static_cast<int>(fy * h);
    editor_key(0x14, false, true);  // T
}

// --list-sprites FILE: every picture the map shows (all of it, its glob eggs expanded), once each, without the NPCs
// and their animations or the editor's markers; one line each: shape, frame, width, height, its class name. The list
// the upscaled sprites are made from (tools/upscale).
// @orig none: port-only tool hook
bool Game::write_map_sprites(const std::string& path) {
    if (!editor_ || !world_) return false;
    std::vector<std::pair<std::uint16_t, std::uint16_t>> seen;
    world_->for_each([&](const Item& item) {
        if (item.off_map || item.parent != 0 || (item.flags & kItemContained) || is_npc_id(item.id)) return;
        const ShapeInfo* info = world_->shape_info(item.shape);
        if (info && (info->is(ShapeInfo::kEditor) || info->is(ShapeInfo::kNpc))) return;
        if (anims_.has_shape(item.shape)) return;  // an actor's shape (its animations), dead or alive
        seen.emplace_back(item.shape, item.frame);
    });
    std::sort(seen.begin(), seen.end());
    seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
    std::ofstream out(path);
    if (!out) return false;
    for (const auto& [shape, frame] : seen) {
        const Shape* s = shapes_.get(shape);
        if (!s || frame >= s->frames().size()) continue;
        const ShapeFrame& f = s->frames()[frame];
        if (f.width <= 0 || f.height <= 0) continue;
        const std::string& name = object_name(shape);
        out << shape << ' ' << frame << ' ' << f.width << ' ' << f.height << ' ' << (name.empty() ? "-" : name) << '\n';
    }
    return true;
}

}  // namespace crusader

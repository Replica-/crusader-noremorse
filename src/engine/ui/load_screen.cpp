#include "engine/ui/load_screen.hpp"

#include "core/flex.hpp"
#include "engine/ui/gump_keys.hpp"

#include <algorithm>
#include <exception>

namespace crusader {

namespace {

constexpr int kMode = 1;  // load
constexpr int kLeft = 111, kTop = 66;
constexpr int kQuarterW = 208, kQuarterH = 174;
constexpr int kSlotX = 183, kSlotY = 109, kSlotStep = 27;
constexpr std::size_t kNameEntry = 9;
constexpr std::size_t kNameSize = 32;

}  // namespace

// @orig 1058:000D
LoadScreen::Names LoadScreen::read_names(const std::filesystem::path& savegame_dir) {
    Names names;
    for (int i = 0; i < kNames; ++i) {
        const auto path = savegame_dir / ("SAVEGAME." + std::to_string(i + 1));
        if (!std::filesystem::exists(path)) continue;
        try {
            const auto flex = FlexArchive::open(path);
            if (flex.count() <= kNameEntry) continue;
            const auto e = flex.entry(kNameEntry);
            const std::size_t n = std::min(e.size(), kNameSize);
            std::string name(e.begin(), e.begin() + n);
            names[i] = name.substr(0, name.find('\0'));
        } catch (const std::exception&) {
            // unreadable: empty name, as for a missing file
        }
    }
    return names;
}

// @orig 1008:0088
LoadScreen::LoadScreen(ShapeCache& gumps, const FontSet& fonts, const Names& names, int& selected)
    : gumps_(&gumps), fonts_(&fonts), names_(&names), selected_(&selected) {
    const Shape* s = gumps_->get(kSlotShape);
    const int w = s && !s->frames().empty() ? s->frames()[0].width : 1;
    const int h = s && !s->frames().empty() ? s->frames()[0].height : 1;
    for (int i = 0; i < kSlots; ++i) {
        const int y = kSlotY + kSlotStep * i;
        slots_[i] = {kSlotX, y, kSlotX + w - 1, y + h - 1};
    }
}

// @orig 1008:05A6 1008:0C9A
void LoadScreen::draw(Surface8& screen) const {
    screen.clear(0);
    for (int q = 0; q < 4; ++q) {
        const auto shape = static_cast<std::uint16_t>(64 + 5 * kMode + q);
        if (const Shape* s = gumps_->get(shape); s && !s->frames().empty()) {
            s->frames()[0].draw(screen, kLeft + (q & 1) * kQuarterW, kTop + (q >> 1) * kQuarterH);
        }
    }
    const Shape* slot = gumps_->get(kSlotShape);
    for (int i = 0; i < kSlots; ++i) {
        const Rect& r = slots_[i];
        if (slot && !slot->frames().empty()) slot->frames()[0].draw(screen, r.x0, r.y0);
        if (i == *selected_) outline(screen, r.x0, r.y0, r.x1, r.y1, kSelectedColour);
        TextWriter(*fonts_, kFont).print(screen, r.x0 + 4, r.y0 + 12, (*names_)[i]);
    }
}

// @orig 1008:0312
void LoadScreen::key(std::uint16_t code) {
    if (result_) return;
    const int sel = *selected_;
    switch (code) {
    case gump_key::kEscape:
        result_ = 0;
        break;
    case gump_key::kUp:
    case '8':
        if (sel != 0) *selected_ = sel - 1;
        break;
    case gump_key::kDown:
    case '2':
        if (sel < kSlots - 1) *selected_ = sel + 1;
        break;
    case gump_key::kEnter:
        choose(sel);
        break;
    default:
        break;
    }
}

// @orig 1008:0AEA 1008:0B63
void LoadScreen::mouse_down(int x, int y) {
    if (result_) return;
    for (int i = 0; i < kSlots; ++i) {
        if (slots_[i].contains(x, y)) {
            *selected_ = i;
            choose(i);
            return;
        }
    }
}

void LoadScreen::choose(int i) {
    if (!(*names_)[i].empty()) result_ = 1;
}

}  // namespace crusader

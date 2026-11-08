// The Silencer in his combat stance (action 4) in each of the 16 facings, with the gun of each weapon class over him as
// the game draws it (WPNOVLAY.DAT), one PNG per class and facing (transparent background, anchor 80, 120 of a 160 x 160
// cell): cC_dDD.png, cn_dDD.png without a gun. tools/angles/make_angles.sh makes the in-between facings from them.
//
// Usage: angleexport <game dir> <out dir>

#include "core/anim_dat.hpp"
#include "core/flex.hpp"
#include "core/palette.hpp"
#include "core/png_writer.hpp"
#include "core/shape.hpp"
#include "core/weapon_overlay.hpp"
#include "engine/world/scene_list.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <map>
#include <string>

using namespace crusader;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: angleexport <game dir> <out dir>\n");
        return 2;
    }
    try {
        const std::filesystem::path dir = argv[1], out = argv[2];
        std::filesystem::create_directories(out);
        const auto flex = FlexArchive::open(dir / "STATIC" / "SHAPES.FLX");
        const auto palette = Palette::load(dir / "STATIC" / "GAMEPAL.PAL");
        const auto anims = AnimDat::load(dir / "STATIC" / "ANIM.DAT");
        const auto overlay = WeaponOverlay::load(dir / "STATIC" / "WPNOVLAY.DAT");
        std::map<int, Shape> shapes;
        const auto shape = [&](int n) -> const Shape& {
            auto found = shapes.find(n);
            if (found == shapes.end()) found = shapes.emplace(n, Shape::parse(flex.entry(static_cast<std::size_t>(n)), layout_for_archive("SHAPES.FLX"))).first;
            return found->second;
        };
        constexpr std::uint16_t kAction = 4;
        const AnimAction* stance = anims.action(1, kAction);
        if (!stance) throw std::runtime_error("no combat stance");
        // One weapon of each class (WeaponOverlay's table), and none.
        const std::pair<const char*, std::uint16_t> classes[] = {{"c0", 0x32E}, {"c1", 0x330}, {"c2", 0x38C}, {"c3", 0x38E},
                                                                 {"c4", 0x332}, {"cn", 0}};
        constexpr int kCell = 160, kAx = 80, kAy = 120;
        for (const auto& [name, weapon] : classes) {
            for (int d = 0; d < 16; ++d) {
                const AnimFrame* f = stance->frame(d, 0);
                Surface8 s(kCell, kCell, kSceneKey);
                shape(1).frames().at(f->frame).draw(s, kAx, kAy, f->mirrored);
                if (weapon != 0) {
                    if (const auto piece = overlay.piece(weapon, kAction, *stance, d, 0); piece && (piece->shape || piece->frame)) {
                        shape(piece->shape).frames().at(piece->frame).draw(s, kAx + piece->dx, kAy + piece->dy);
                    }
                }
                char file[32];
                std::snprintf(file, sizeof file, "%s_d%02d.png", name, d);
                write_png(out / file, s, palette, kSceneKey);
            }
        }
        std::printf("combat stance, 6 sets of 16 facings -> %s\n", out.string().c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

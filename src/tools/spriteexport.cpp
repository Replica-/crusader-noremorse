// Writes the frames of a sprite list (crusader --list-sprites) as PNGs with a transparent background, one file per
// frame, SSSS_FFF.png (shape and frame in decimal): the pictures tools/upscale makes the upscaled sprites from.
//
// Usage: spriteexport <game dir> <list.txt> <out dir>

#include "core/flex.hpp"
#include "core/palette.hpp"
#include "core/png_writer.hpp"
#include "core/shape.hpp"
#include "engine/world/scene_list.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace crusader;

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: spriteexport <game dir> <list.txt> <out dir>\n");
        return 2;
    }
    try {
        const std::filesystem::path dir = argv[1], out = argv[3];
        const auto flex = FlexArchive::open(dir / "STATIC" / "SHAPES.FLX");
        const auto palette = Palette::load(dir / "STATIC" / "GAMEPAL.PAL");
        std::filesystem::create_directories(out);
        std::map<int, Shape> shapes;
        std::ifstream list(argv[2]);
        std::string line;
        int written = 0;
        while (std::getline(list, line)) {
            std::istringstream words(line);
            int shape_no = 0, frame_no = 0;
            if (!(words >> shape_no >> frame_no)) continue;
            auto found = shapes.find(shape_no);
            if (found == shapes.end()) {
                found = shapes.emplace(shape_no, Shape::parse(flex.entry(static_cast<std::size_t>(shape_no)),
                                                              layout_for_archive("SHAPES.FLX"))).first;
            }
            const auto& frames = found->second.frames();
            if (frame_no < 0 || static_cast<std::size_t>(frame_no) >= frames.size()) continue;
            const ShapeFrame& f = frames[static_cast<std::size_t>(frame_no)];
            if (f.width <= 0 || f.height <= 0) continue;
            // As the hardware scene makes its textures: the frame alone, the scene's key colour left clear.
            Surface8 s(f.width, f.height, kSceneKey);
            f.draw(s, f.xoff, f.yoff);
            char name[32];
            std::snprintf(name, sizeof name, "%04d_%03d.png", shape_no, frame_no);
            write_png(out / name, s, palette, kSceneKey);
            ++written;
        }
        std::printf("%d frames -> %s\n", written, out.string().c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

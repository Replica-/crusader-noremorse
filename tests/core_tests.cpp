// Game configuration parsing and writing (no game data).

#include "core/game_config.hpp"
#include "core/iso_image.hpp"
#include "core/palette.hpp"
#include "core/png_reader.hpp"
#include "core/png_writer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace crusader;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void check_config() {
    // As installed, CRUSADER.CFG has none of the option keys but "bigvideo"
    // (the data's may have been rewritten by the options screen since).
    const GameConfig c = GameConfig::parse("irq=7\r\ndma=1\r\nbigvideo=true\r\nsubtitles=true\r\n");
    check(c.sound_volume == 0x40 && c.music_volume == 0x20 && c.filter && c.mouse_speed == 2, "config defaults");
    check(c.big_video && c.animation == 0 && c.frameskip == 0 && !c.limit_blasts, "config bigvideo");
    // A missing file gives the defaults.
    const GameConfig none = GameConfig::load("NO_SUCH.CFG");
    check(none.music_volume == 0x20 && !none.big_video && none.mouse_speed == 2, "config missing");

    GameConfig e = GameConfig::parse("irq=7\r\nmusicvolume=12\r\nbigvideo=true\r\nfilter=false\r\nlimitblasts=true\r\n");
    check(e.music_volume == 12 && e.big_video && !e.filter && e.limit_blasts, "config parse");
    e.music_volume = 40;
    e.big_video = false;
    e.frameskip = 3;
    check(e.text() == "irq=7\r\nmusicvolume=40\r\nfilter=false\r\nlimitblasts=true\r\nsoundvolume=64\r\n"
                      "mousespeed=2\r\nanimation=0\r\nframeskip=3\r\n",
          "config written over its lines, other keys kept");

    // "noweasel": read, and written only when set (1020:0C7F).
    GameConfig w = GameConfig::parse("irq=7\r\nnoweasel=true\r\n");
    check(w.no_weasel && !GameConfig::parse("noweasel=false\r\n").no_weasel, "config noweasel");
    w.no_weasel = false;
    check(w.text().find("noweasel") == std::string::npos, "config noweasel erased when clear");

    // "followplayer" (port addition): "true" centres, "delay" the delay follow, missing the original's jumps.
    GameConfig follow = GameConfig::parse("irq=7\r\nfollowplayer=delay\r\n");
    check(follow.follow_player == 2 && GameConfig::parse("followplayer=true\r\n").follow_player == 1, "config follow modes");
    follow.follow_player = 0;
    check(follow.text().find("followplayer") == std::string::npos, "config follow erased when off");

    // "upscaledsprites" (port addition): the method's folder, off when missing; the first version's "true" is degrain.
    GameConfig up = GameConfig::parse("irq=7\r\n");
    check(up.upscaled_sprites == 0 && up.text().find("upscaledsprites") == std::string::npos, "config upscaled off");
    up.upscaled_sprites = 2;
    check(up.text().find("upscaledsprites=sdxl55") != std::string::npos && GameConfig::parse(up.text()).upscaled_sprites == 2,
          "config upscaled sdxl55 round trip");
    check(GameConfig::parse("upscaledsprites=degrain\r\n").upscaled_sprites == 1 &&
              GameConfig::parse("upscaledsprites=true\r\n").upscaled_sprites == 1,
          "config upscaled degrain");

    // "autosave" (port addition): on by default, written only when switched off.
    GameConfig saving = GameConfig::parse("irq=7\r\n");
    check(saving.autosave && saving.text().find("autosave") == std::string::npos, "config autosave default on, not written");
    saving.autosave = false;
    check(!GameConfig::parse(saving.text()).autosave && saving.text().find("autosave=false") != std::string::npos,
          "config autosave off round trip");

    // Controller settings (port addition): defaults, not written until changed.
    GameConfig p = GameConfig::parse("irq=7\r\n");
    check(p.pad.is_default() && p.text().find("pad") == std::string::npos, "config pad defaults not written");
    p.pad.buttons[kPadA] = kPadMedkit;
    p.pad.aim = 5;
    p.pad.dead_zone = 0;
    const GameConfig q = GameConfig::parse(p.text());
    check(q.pad.buttons == p.pad.buttons && q.pad.aim == 5 && q.pad.dead_zone == 0, "config pad round trip");
    check(GameConfig::parse("padaim=99\r\npaddeadzone=-3\r\npadmap=1,99,2\r\n").pad.aim == 5, "config pad aim clamped");
    const GameConfig c2 = GameConfig::parse("paddeadzone=-3\r\npadmap=1,99,x\r\n");
    check(c2.pad.dead_zone == 0 && c2.pad.buttons[1] == kPadActionCount - 1 && c2.pad.buttons[2] == kPadNone &&
              c2.pad.buttons[3] == PadSettings{}.buttons[3],
          "config pad values clamped, short list keeps defaults");
    // assign(): the button's old action and the action's old button are unbound.
    PadSettings a;
    a.assign(kPadB, kPadFire);  // fire was on RT, jump on B
    check(a.buttons[kPadB] == kPadFire && a.buttons[kPadRightTrigger] == kPadNone && a.button_of(kPadJump) < 0 &&
              a.button_of(kPadFire) == kPadB,
          "pad assign moves the action");
    // A short list leaves the other inputs at their defaults; a direction round-trips.
    check(c2.pad.buttons[kPadRightStickUp] == kPadDefaultRole && c2.pad.buttons[kPadDpadRight] == PadSettings{}.buttons[kPadDpadRight],
          "config pad directions default");
    PadSettings dir;
    dir.assign(kPadRightStickLeft, kPadStepLeft);
    GameConfig dc;
    dc.pad = dir;
    check(GameConfig::parse(dc.text()).pad.buttons == dir.buttons, "config pad direction round trip");
    p.pad = PadSettings{};
    check(p.text().find("pad") == std::string::npos, "config pad erased when back to defaults");
}

// A minimal ISO 9660 image: root with A.TXT;1 and a directory D holding B.BIN;1.
void check_iso() {
    namespace fs = std::filesystem;
    std::vector<unsigned char> img(24 * 2048, 0);
    auto put32 = [&](std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) img[at + i] = static_cast<unsigned char>(v >> (8 * i));
    };
    auto record = [&](std::size_t at, std::uint32_t lba, std::uint32_t size, bool dir, const std::string& name) {
        const std::size_t n = name.size();
        img[at] = static_cast<unsigned char>(33 + n + ((n % 2) ? 0 : 1));
        put32(at + 2, lba);
        put32(at + 10, size);
        img[at + 25] = dir ? 2 : 0;
        img[at + 32] = static_cast<unsigned char>(n);
        std::memcpy(&img[at + 33], name.data(), n);
        return at + img[at];
    };
    img[16 * 2048] = 1;
    std::memcpy(&img[16 * 2048 + 1], "CD001", 5);
    record(16 * 2048 + 156, 18, 2048, true, std::string(1, 0));
    std::size_t at = 18 * 2048;
    at = record(at, 18, 2048, true, std::string(1, 0));
    at = record(at, 18, 2048, true, std::string(1, 1));
    at = record(at, 20, 5, false, "A.TXT;1");
    record(at, 19, 2048, true, "D");
    record(19 * 2048, 21, 3, false, "B.BIN;1");
    std::memcpy(&img[20 * 2048], "hello", 5);
    std::memcpy(&img[21 * 2048], "abc", 3);
    const fs::path dir = fs::temp_directory_path() / "crusader_iso_test";
    std::error_code ignored0;
    fs::remove_all(dir, ignored0);
    fs::create_directories(dir);
    std::ofstream(dir / "t.iso", std::ios::binary).write(reinterpret_cast<const char*>(img.data()), img.size());

    {
    const IsoImage iso = IsoImage::open(dir / "t.iso");
    check(iso.files().size() == 2, "iso lists two files");
    iso.extract(dir / "out", {});
    std::string a, b;
    std::ifstream(dir / "out/A.TXT") >> a;
    std::ifstream(dir / "out/D/B.BIN") >> b;
    check(a == "hello" && b == "abc", "iso extracts files and subdirectories");
    iso.extract(dir / "skip", {"D"});
    check(fs::exists(dir / "skip/A.TXT") && !fs::exists(dir / "skip/D"), "iso skips a directory");
    }  // the image is closed before the folder is removed

    // prepare_game_data: an ISO in a data folder without STATIC/ is extracted.
    fs::create_directories(dir / "data");
    fs::copy_file(dir / "t.iso", dir / "data/game.iso");
    prepare_game_data(dir / "data");
    check(fs::exists(dir / "data/D/B.BIN"), "prepare_game_data extracts the ISO of data/");
    prepare_game_data(dir / "fresh");
    check(fs::exists(dir / "fresh/PUT_GAME_DATA_HERE.txt"), "prepare_game_data creates data/ with its note");
    std::error_code ignored;
    fs::remove_all(dir, ignored);
}

// PNG reading: two pictures made by another encoder (PIL: dynamic Huffman codes, every filter), then one written by
// png_writer (stored blocks, a palette with a transparent colour).
void check_png() {
    static const std::uint8_t kRgba[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x07,
        0x00, 0x00, 0x00, 0x05, 0x08, 0x06, 0x00, 0x00, 0x00, 0x89, 0x9A, 0xF6, 0xD8, 0x00, 0x00, 0x00, 0x48, 0x49, 0x44, 0x41,
        0x54, 0x78, 0xDA, 0x55, 0xCC, 0xB1, 0x09, 0xC0, 0x30, 0x10, 0x03, 0xC0, 0x0B, 0xB8, 0xF5, 0x02, 0xDF, 0x1B, 0x2F, 0xE0,
        0xD6, 0xBB, 0xE4, 0x27, 0xFB, 0x55, 0xD3, 0x98, 0x90, 0x74, 0x27, 0x84, 0x74, 0xE1, 0x1E, 0xD4, 0xC0, 0x20, 0xBF, 0x6E,
        0xB6, 0xA2, 0xA3, 0x27, 0xFD, 0xE7, 0x66, 0x3B, 0x21, 0x8A, 0x40, 0xBC, 0x6E, 0xB6, 0xB3, 0x08, 0xCC, 0x64, 0x16, 0x13,
        0xF3, 0x7B, 0x1B, 0xA7, 0x58, 0x58, 0xC9, 0xAA, 0x07, 0x72, 0xA0, 0x10, 0x6D, 0xCD, 0x90, 0x31, 0x6F, 0x00, 0x00, 0x00,
        0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    static const std::uint8_t kRgb[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x07,
        0x00, 0x00, 0x00, 0x05, 0x08, 0x02, 0x00, 0x00, 0x00, 0x06, 0xF8, 0x61, 0x8F, 0x00, 0x00, 0x00, 0x24, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9C, 0x63, 0x64, 0x60, 0x60, 0x50, 0xC5, 0x40, 0x2C, 0x0C, 0xB6, 0x0C, 0x0C, 0x0C, 0xBC, 0x68, 0x08, 0x2E,
        0x2A, 0x85, 0x8C, 0x90, 0x45, 0xD5, 0xE1, 0x08, 0x4D, 0xD4, 0x04, 0x82, 0x00, 0xDC, 0x36, 0x04, 0x6E, 0x7F, 0xDA, 0x91,
        0xD7, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    // Both: red x * 37, green y * 61, blue x * y * 13 (mod 256); the first also alpha 96 where (x + y) % 3 == 0.
    const auto expected = [](int x, int y, bool alpha) {
        const std::uint32_t a = alpha && (x + y) % 3 == 0 ? 96u : 255u;
        return a << 24 | std::uint32_t((x * 37) % 256) << 16 | std::uint32_t((y * 61) % 256) << 8 | std::uint32_t((x * y * 13) % 256);
    };
    for (const bool alpha : {true, false}) {
        const auto img = alpha ? decode_png(kRgba) : decode_png(kRgb);
        check(img && img->width == 7 && img->height == 5, alpha ? "png rgba size" : "png rgb size");
        bool same = img.has_value();
        for (int y = 0; img && y < 5; ++y) {
            for (int x = 0; x < 7; ++x) same = same && img->pixels[static_cast<std::size_t>(y * 7 + x)] == expected(x, y, alpha);
        }
        check(same, alpha ? "png rgba pixels" : "png rgb pixels");
    }
    check(!decode_png(std::span(kRgb, 40)), "png cut short");

    Palette pal;
    pal.colors[3] = {10, 20, 30};
    pal.colors[200] = {250, 128, 1};
    Surface8 s(3, 2, 255);
    s.pixels[0] = 3, s.pixels[4] = 200;
    const auto path = std::filesystem::temp_directory_path() / "crusader_png_test.png";
    write_png(path, s, pal, std::uint8_t{255});
    const auto back = read_png(path);
    check(back && back->width == 3 && back->height == 2, "png indexed size");
    check(back && back->pixels[0] == 0xFF0A141Eu && back->pixels[4] == 0xFFFA8001u && back->pixels[1] >> 24 == 0,
          "png indexed pixels, transparent colour");
    std::filesystem::remove(path);

    // write_png_rgb (the screenshots): rows a pitch apart, read back as opaque RGB.
    const std::uint32_t shot[2 * 4] = {0xFF102030u, 0xFFFFFFFFu, 0xDEADBEEFu, 0xDEADBEEFu,
                                       0xFF000000u, 0xFF7F80FEu, 0xDEADBEEFu, 0xDEADBEEFu};
    write_png_rgb(path, 2, 2, shot, 4 * 4);
    const auto rgb = read_png(path);
    check(rgb && rgb->width == 2 && rgb->height == 2 && rgb->pixels[0] == 0xFF102030u && rgb->pixels[1] == 0xFFFFFFFFu &&
              rgb->pixels[2] == 0xFF000000u && rgb->pixels[3] == 0xFF7F80FEu,
          "png rgb written and read back");
    std::filesystem::remove(path);
}

}  // namespace

int main() {
    check_config();
    check_iso();
    check_png();
    std::printf(failures ? "%d failure(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}

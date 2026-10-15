// Game configuration parsing and writing (no game data).

#include "core/game_config.hpp"

#include <cstdio>
#include <string>

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
    check(c2.pad.buttons[kPadLeftStickUp] == kPadDefaultRole && c2.pad.buttons[kPadDpadRight] == PadSettings{}.buttons[kPadDpadRight],
          "config pad directions default");
    PadSettings dir;
    dir.assign(kPadRightStickLeft, kPadStepLeft);
    GameConfig dc;
    dc.pad = dir;
    check(GameConfig::parse(dc.text()).pad.buttons == dir.buttons, "config pad direction round trip");
    p.pad = PadSettings{};
    check(p.text().find("pad") == std::string::npos, "config pad erased when back to defaults");
}

}  // namespace

int main() {
    check_config();
    std::printf(failures ? "%d failure(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}

#pragma once
// Game controller settings (port addition, the original has no gamepad): what
// each button does, and the right stick's response. Stored in CRUSADER.CFG
// (game_config.hpp), read by the SDL layer (platform/sdl_gamepad.hpp) and
// edited by the controller screen (engine/ui/gamepad_screen.hpp).
//
// An action is a key of the original controls: the gamepad sends it as the
// keyboard would, so the engine needs nothing more.

#include <array>
#include <cstdint>

namespace crusader {

struct PadAction {
    const char* label;         // shown by the screen (French, the port's target)
    std::uint8_t scancode;     // PC set 1; 0 for "none"
    bool extended;
    std::uint8_t ascii;
    std::uint8_t modifier = 0;  // scancode of a key held with it (Ctrl 0x1D, Alt 0x38, Shift 0x2A)
};

// The moves are the original's key combinations (docs/animation.md): Ctrl +
// left / right roll, Alt + left / right step aside, Ctrl + down kneels or
// stands, Shift + up runs.
enum PadActionId : int {
    kPadNone, kPadFire, kPadRun, kPadRollLeft, kPadRollRight, kPadStepLeft, kPadStepRight, kPadCrouch,
    kPadShift, kPadPrevWeapon, kPadNextWeapon, kPadPrevItem, kPadNextItem, kPadUseItem, kPadMedkit,
    kPadJump, kPadMenu, kPadConfirm, kPadSelect,
    kPadActionCount,
};

constexpr std::uint8_t kScShift = 0x2A, kScCtrl = 0x1D, kScAlt = 0x38;

constexpr std::array<PadAction, kPadActionCount> kPadActions = {{
    {"AUCUNE", 0, false, 0},
    {"TIR", 0x39, false, ' '},
    {"COURIR", 0x48, true, 0, kScShift},
    {"ROULADE GAUCHE", 0x4B, true, 0, kScCtrl},
    {"ROULADE DROITE", 0x4D, true, 0, kScCtrl},
    {"PAS A GAUCHE", 0x4B, true, 0, kScAlt},
    {"PAS A DROITE", 0x4D, true, 0, kScAlt},
    {"SE BAISSER OU LEVER", 0x50, true, 0, kScCtrl},
    {"MAJ", 0x2A, false, 0},
    {"ARME PRECEDENTE", 0x10, false, 'q'},
    {"ARME SUIVANTE", 0x11, false, 'w'},
    {"OBJET PRECEDENT", 0x17, false, 'i'},
    {"OBJET SUIVANT", 0x18, false, 'o'},
    {"UTILISER OBJET", 0x16, false, 'u'},
    {"TROUSSE", 0x32, false, 'm'},
    {"SAUT", 0x0F, false, '\t'},
    {"MENU", 0x01, false, 27},
    {"VALIDER", 0x1C, false, '\r'},
    {"SELECTIONNER", 0x1F, false, 's'},  // S: the selection mode, then the next object
}};

// The inputs of the controller, in the order of PadSettings::buttons: the
// buttons and triggers, then the four directions (up, down, left, right) of the
// left stick, of the right stick and of the d-pad.
enum PadButton : int {
    kPadA, kPadB, kPadX, kPadY, kPadLeftBumper, kPadRightBumper, kPadLeftTrigger, kPadRightTrigger,
    kPadLeftStick, kPadRightStick, kPadStart, kPadBack,
    kPadLeftStickUp, kPadLeftStickDown, kPadLeftStickLeft, kPadLeftStickRight,
    kPadRightStickUp, kPadRightStickDown, kPadRightStickLeft, kPadRightStickRight,
    kPadDpadUp, kPadDpadDown, kPadDpadLeft, kPadDpadRight,
    kPadButtonCount,
};
constexpr int kPadFirstDirection = kPadLeftStickUp;

// A direction bound to this keeps its role: arrow key (left stick, d-pad) or
// mouse motion (right stick). A button bound to kPadNone does nothing.
constexpr int kPadDefaultRole = -1;

// How the screen names each input.
constexpr std::array<const char*, kPadButtonCount> kPadButtonLabels = {
    "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "CLIC STICK GAUCHE", "CLIC STICK DROIT", "START", "SELECT",
    "STICK G HAUT", "STICK G BAS", "STICK G GAUCHE", "STICK G DROITE",
    "STICK D HAUT", "STICK D BAS", "STICK D GAUCHE", "STICK D DROITE",
    "CROIX HAUT", "CROIX BAS", "CROIX GAUCHE", "CROIX DROITE",
};

struct PadSettings {
    static constexpr int kAimLevels = 5;       // right stick speed, 1..5
    static constexpr int kDeadZoneLevels = 5;  // right stick dead zone, 0..4
    static constexpr int kDefaultAim = 3;
    static constexpr int kDefaultDeadZone = 2;
    // Pixels per second at full tilt, and the dead zone (of 32767), by level.
    static constexpr std::array<int, kAimLevels> kAimSpeeds = {300, 450, 700, 950, 1250};
    static constexpr std::array<int, kDeadZoneLevels> kDeadZones = {3000, 5000, 7000, 10000, 13000};

// @orig none: port-only gamepad settings
    
    static constexpr std::array<int, kPadButtonCount> default_buttons() {
        std::array<int, kPadButtonCount> b{};
        for (int& a : b) a = kPadDefaultRole;
        b[kPadA] = kPadSelect;
        b[kPadB] = kPadJump;
        b[kPadX] = kPadConfirm;
        b[kPadY] = kPadNextWeapon;
        b[kPadLeftBumper] = kPadStepLeft;
        b[kPadRightBumper] = kPadStepRight;
        b[kPadLeftTrigger] = kPadNone;
        b[kPadRightTrigger] = kPadFire;
        b[kPadLeftStick] = kPadShift;
        b[kPadRightStick] = kPadCrouch;
        b[kPadStart] = kPadMenu;
        b[kPadBack] = kPadNone;
        // Right stick left / right roll (so it no longer moves the mouse sideways),
        // up / down keep the aim; the d-pad uses the inventory and the medkit.
        b[kPadRightStickLeft] = kPadRollLeft;
        b[kPadRightStickRight] = kPadRollRight;
        b[kPadDpadUp] = kPadUseItem;
        b[kPadDpadDown] = kPadMedkit;
        b[kPadDpadLeft] = kPadPrevItem;
        b[kPadDpadRight] = kPadNextItem;
        return b;
    }
    // The action (PadActionId) of each input, or kPadDefaultRole.
    std::array<int, kPadButtonCount> buttons = default_buttons();
    int aim = kDefaultAim;            // 1..kAimLevels
    int dead_zone = kDefaultDeadZone;  // 0..kDeadZoneLevels - 1

    // The input bound to `action`, or -1.
// @orig none: port-only gamepad settings
    
    int button_of(int action) const {
        for (int b = 0; b < kPadButtonCount; ++b) {
            if (buttons[b] == action) return b;
        }
        return -1;
    }
    // Binds `action` to `button`: the input's previous action is unbound, and
    // so is the input `action` was on (an action has one input).
    void assign(int button, int action) {
        unbind(action);
        buttons[button] = action;
    }
    // Takes `action` off its input: a button then does nothing, a direction
    // goes back to its role.
    void unbind(int action) {
        for (int b = 0; b < kPadButtonCount; ++b) {
            if (buttons[b] == action) buttons[b] = b >= kPadFirstDirection ? kPadDefaultRole : kPadNone;
        }
    }

    bool is_default() const {
        const PadSettings d;
        return buttons == d.buttons && aim == d.aim && dead_zone == d.dead_zone;
    }
};

}  // namespace crusader

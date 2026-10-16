#pragma once
// Game controller (Xbox or any SDL_GameController) -> the keyboard and mouse
// events the game already understands. The original has no gamepad (only the
// PC joystick, which the port does not reproduce), so each input is turned
// into a key of the original controls, or a combination of keys.
//
// The defaults (the author's settings, PadSettings in core/pad_config.hpp):
//   left stick          up / down: forward / backward, left / right: step aside   RT  fire (Space)
//   right stick         up / down: aim (mouse)     right stick left / right  turn left / right
//   A / B / X / Y       select (S) / jump / confirm / next weapon
//   LB / RB             roll left / right          L3 / R3  run (Shift + up) / kneel or stand (Ctrl + down)
//   d-pad               use item / medkit / previous item / next item
//   Start               menu
// The actions of the inputs, and the right stick's speed and dead zone, come
// from PadSettings, set by the controller screen. An action may be a
// combination (Ctrl + left rolls, Alt + left steps aside): the modifier goes
// down first and up last.
//
// The inputs are the buttons, the triggers and the directions of the d-pad
// and of both sticks (PadButton). A direction left on its default keeps its
// role (arrow key, or mouse motion for the right stick); once bound to an
// action it fires that action instead, with a hysteresis on the stick, and
// the right stick no longer moves the mouse that way. In menus, message boxes
// and screens A, B and Start are Enter, Esc and Esc, and the directions keep
// their roles (arrows, mouse), whatever they are bound to.

#include "core/pad_config.hpp"
#include "platform/sdl_input.hpp"

#include <array>
#include <cstdint>
#include <vector>

union SDL_Event;
struct _SDL_GameController;

namespace crusader {

class Gamepad {
public:
    Gamepad() = default;
    ~Gamepad();
    Gamepad(const Gamepad&) = delete;
    Gamepad& operator=(const Gamepad&) = delete;

    // Initialises the SDL subsystem and opens the controllers already plugged in.
    void init();

    // The settings in use (read at every event); the default ones until set.
    void set_settings(const PadSettings* settings) { settings_ = settings ? settings : &defaults_; }

    // Feeds one SDL event (device hot-plug, buttons, axes). Returns true when
    // the event belonged to the controller.
    bool feed(const SDL_Event& ev);

    // While on, a press of an input (button, trigger or direction) sends no
    // key: it is kept for take_captured(), so the controller screen can bind
    // it. The arrow keys of the d-pad and left stick are held back as well.
    void set_capture(bool on);
    // Inputs (PadButton) pressed while capturing, since the last call.
    std::vector<int> take_captured();

    // True in menus, message boxes and screens: A confirms, B cancels.
    void set_menu(bool menu);

    // Keys produced since the last call.
    std::vector<KeyEvent> take_keys();

    // Right stick as a mouse motion over `seconds` (sub-pixel remainders kept).
    void mouse_motion(double seconds, int* dx, int* dy);

    bool connected() const { return controller_ != nullptr; }

private:
    enum Dir { kUp, kDown, kLeft, kRight };

    void button(int sdl_button, bool pressed);
    void press_slot(int slot, bool pressed);
    void axis(int sdl_axis, int value);
    void stick(int first_slot, Dir negative, Dir positive, int value);
    void direction(int slot, bool on);
    void refresh_arrows();
    bool has_role(int slot) const;  // a direction acting as arrow key or mouse
    void trigger(int index, bool on);
    void press_key(std::uint8_t scancode, bool extended, bool pressed, std::uint8_t ascii = 0);
    void open(int device_index);
    void release_all();

    _SDL_GameController* controller_ = nullptr;
    int instance_ = -1;
    bool menu_ = false;
    bool capture_ = false;
    std::vector<int> captured_;
    bool active_[kPadButtonCount] = {};  // direction inputs past their threshold
    bool arrow_down_[4] = {};            // the arrow key as last sent
    bool held_[32] = {};       // buttons down
    PadSettings defaults_;
    const PadSettings* settings_ = &defaults_;
    // The key each configurable button sent when pressed, for its release.
    std::array<const PadAction*, kPadButtonCount> sent_{};
    bool triggers_[2] = {};
    int right_x_ = 0, right_y_ = 0;
    double rem_x_ = 0, rem_y_ = 0;
    std::uint16_t flags_ = 0;  // modifiers held (Ctrl, Alt, Shift)
    std::vector<KeyEvent> keys_;
};

}  // namespace crusader

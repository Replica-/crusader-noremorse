// SDL event -> KeyEvent / MouseEvent translation, using synthetic events
// (SDL itself is not initialised).

#define SDL_MAIN_HANDLED
#include "engine/input/mouse_queue.hpp"
#include "platform/sdl_input.hpp"

#include <SDL.h>

#include <cstdio>

using namespace crusader;

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

SDL_Event key(Uint32 type, SDL_Scancode sc, SDL_Keycode sym, Uint16 mod = KMOD_NONE) {
    SDL_Event ev{};
    ev.type = type;
    ev.key.keysym.scancode = sc;
    ev.key.keysym.sym = sym;
    ev.key.keysym.mod = mod;
    return ev;
}

void test_scancodes() {
    bool ext = true;
    CHECK(pc_scancode(SDL_SCANCODE_ESCAPE, &ext) == 0x01 && !ext);
    CHECK(pc_scancode(SDL_SCANCODE_A) == 0x1E);
    CHECK(pc_scancode(SDL_SCANCODE_SPACE) == 0x39);
    CHECK(pc_scancode(SDL_SCANCODE_F10) == 0x44);
    CHECK(pc_scancode(SDL_SCANCODE_UP, &ext) == 0x48 && ext);
    CHECK(pc_scancode(SDL_SCANCODE_KP_8, &ext) == 0x48 && !ext);
    CHECK(pc_scancode(SDL_SCANCODE_RCTRL, &ext) == 0x1D && ext);
    CHECK(pc_scancode(SDL_SCANCODE_APPLICATION) == 0);
}

void test_keys() {
    auto k = translate_key(key(SDL_KEYDOWN, SDL_SCANCODE_A, SDLK_a));
    CHECK(k && k->pressed && k->scancode == 0x1E && k->ascii == 'a');

    k = translate_key(key(SDL_KEYDOWN, SDL_SCANCODE_A, SDLK_a, KMOD_LSHIFT));
    CHECK(k && k->ascii == 'A' && k->shift() && (k->flags & kLeftShift));

    k = translate_key(key(SDL_KEYDOWN, SDL_SCANCODE_A, SDLK_a, KMOD_CAPS | KMOD_RSHIFT));
    CHECK(k && k->ascii == 'a');  // caps + shift cancel out

    k = translate_key(key(SDL_KEYUP, SDL_SCANCODE_A, SDLK_a));
    CHECK(k && !k->pressed && k->ascii == 0);

    k = translate_key(key(SDL_KEYDOWN, SDL_SCANCODE_LEFT, SDLK_LEFT, KMOD_RCTRL));
    CHECK(k && k->extended && k->scancode == 0x4B && k->ascii == 0 && (k->flags & kRightCtrl));

    k = translate_key(key(SDL_KEYDOWN, SDL_SCANCODE_RETURN, SDLK_RETURN));
    CHECK(k && k->ascii == '\r');

    SDL_Event other{};
    other.type = SDL_MOUSEMOTION;
    CHECK(!translate_key(other));
}

void test_mouse() {
    SDL_Event ev{};
    ev.type = SDL_MOUSEMOTION;
    ev.motion.x = 320;
    ev.motion.y = 240;
    ev.motion.xrel = -3;
    ev.motion.yrel = 4;
    auto m = translate_mouse(ev);
    CHECK(m && m->kind == MouseEvent::Kind::Move && m->x == 320 && m->y == 240 && m->dx == -3 && m->dy == 4);

    ev = SDL_Event{};
    ev.type = SDL_MOUSEBUTTONDOWN;
    ev.button.button = SDL_BUTTON_RIGHT;
    ev.button.x = 10;
    ev.button.y = 20;
    m = translate_mouse(ev);
    CHECK(m && m->kind == MouseEvent::Kind::ButtonDown && m->button == 2 && m->x == 10);
}

// MOUSECTL.C: clicks held back 13 ticks, double clicks, both buttons, motion.
void test_mouse_queue() {
    MouseQueue q;
    auto ticks = [&](int n) {
        for (int i = 0; i < n; ++i) q.tick();
    };
    // A click: nothing for 12 ticks, then the press and its release.
    q.press(false, 10, 20, 1);
    q.release(false, 11, 21, 0);
    ticks(12);
    CHECK(!q.next());
    ticks(1);
    auto e = q.next();
    CHECK(e && e->type == MouseInput::kPress && e->code == MouseInput::kLeftDown && e->kind() == MouseInput::kLeft &&
          e->x == 10 && e->buttons == 1);
    e = q.next();
    CHECK(e && e->type == MouseInput::kRelease && e->code == MouseInput::kLeftUp && e->x == 11);
    CHECK(!q.next());

    // A double click: the second press at once, the first click lost.
    q.press(true, 0, 0, 2);
    q.release(true, 0, 0, 0);
    ticks(5);
    q.press(true, 0, 0, 2);
    e = q.next();
    CHECK(e && e->type == MouseInput::kDoubleClick && e->code == MouseInput::kRightDown &&
          e->kind() == MouseInput::kRight);
    CHECK(!q.next());
    ticks(20);
    CHECK(!q.next());
    q.release(true, 0, 0, 0);  // nothing held back: at once
    e = q.next();
    CHECK(e && e->type == MouseInput::kRelease && e->code == MouseInput::kRightUp);

    // Both buttons: one press, both dropped.
    q.press(false, 0, 0, 1);
    q.press(true, 0, 0, 3);
    e = q.next();
    CHECK(e && e->type == MouseInput::kPress && e->code == MouseInput::kBothDown && e->kind() == MouseInput::kBoth);
    ticks(13);
    CHECK(!q.next());

    // Motion: only the latest, after the queued events.
    q.move(1, 1, 0);
    q.move(5, 6, 0);
    q.release(false, 7, 7, 0);
    e = q.next();
    CHECK(e && e->type == MouseInput::kRelease);
    e = q.next();
    CHECK(e && e->type == MouseInput::kMotion && e->kind() == MouseInput::kMotionKind && e->x == 5 && e->y == 6);
    CHECK(!q.next());

    // Ten queued events at most.
    for (int i = 0; i < 12; ++i) q.release(false, i, 0, 0);
    int n = 0;
    while (q.next()) ++n;
    CHECK(n == 10);
}

}  // namespace

int main() {
    SDL_SetMainReady();
    test_scancodes();
    test_keys();
    test_mouse();
    test_mouse_queue();
    std::printf(failures ? "%d failure(s)\n" : "all input checks passed\n", failures);
    return failures ? 1 : 0;
}

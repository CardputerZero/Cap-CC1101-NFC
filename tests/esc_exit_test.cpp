#include "core/nfc_app.hpp"

#include <core/hal/hal.hpp>
#include <cstdlib>
#include <iostream>

#if LV_USE_SDL
#include <SDL2/SDL.h>

// Replace physical keyboard state to test LVGL's synthetic releases headlessly.
static Uint8 keyboard_state[SDL_NUM_SCANCODES]{};
extern "C" const Uint8* SDLCALL SDL_GetKeyboardState(int* count)
{
    if (count) *count = SDL_NUM_SCANCODES;
    return keyboard_state;
}
#endif

namespace {
using App = cap_nfc::NfcApp;
uint32_t now_ms = 0;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void esc(App& app, bool pressed)
{
#if LV_USE_SDL
    keyboard_state[SDL_SCANCODE_ESCAPE] = pressed;
#endif
    app.onLvglKeyState(LV_KEY_ESC, nullptr, pressed);
}

void advance(App& app, uint32_t elapsed)
{
    now_ms += elapsed;
    app.tick(now_ms);
}

void checkHold(App& app)
{
    esc(app, true);
    advance(app, 2999);
    require(!app.quitRequested(), "ESC exited before three seconds");
    esc(app, true);
    advance(app, 1);
    require(app.quitRequested(), "held ESC must exit without a launcher signal or repeat timer reset");
    esc(app, false);
}

void testExit()
{
    App app;
    app.start();
    esc(app, true);
    advance(app, 1000);
    esc(app, false);
    advance(app, 3000);
    require(!app.quitRequested(), "releasing ESC must cancel exit");
#if LV_USE_SDL
    esc(app, true);
    advance(app, 1000);
    keyboard_state[SDL_SCANCODE_ESCAPE] = 0;
    advance(app, 3000);
    require(!app.quitRequested(), "SDL keyup without an LVGL release must cancel exit");
#endif
    checkHold(app);

    esc(app, true);
    app.stop();
    app.start();
    advance(app, 4000);
    require(!app.quitRequested(), "restarting must clear the previous hold and exit request");
    esc(app, false);
    checkHold(app);
}

void testNavigation()
{
    App app;
    app.start();
    app.onKey(cap_nfc::nfc_key::Help);
    esc(app, true);
    esc(app, true);
    advance(app, 4000);
    require(!app.quitRequested(), "ESC closing help must not arm exit until released");
    esc(app, false);

    app.onKey(cap_nfc::nfc_key::Right);
    esc(app, true);
    esc(app, true);
    advance(app, 4000);
    require(!app.quitRequested(), "ESC returning from details must not arm exit until released");
    esc(app, false);

    esc(app, true);
    app.onKey(cap_nfc::nfc_key::Help);
    advance(app, 4000);
    require(!app.quitRequested(), "opening help must cancel a pending exit");
    esc(app, false);
    esc(app, true);
    esc(app, false);
    checkHold(app);
}

void testTickWrapAndSyntheticRelease()
{
    App app;
    app.start();
    now_ms = UINT32_MAX - 1000;
    esc(app, true);
#if LV_USE_SDL
    app.onLvglKeyState(LV_KEY_ESC, nullptr, false);
#endif
    advance(app, 3000);
    require(app.quitRequested(), "hold must survive tick wrap and SDL's synthetic release");
    esc(app, false);
}
}  // namespace

int main()
{
    lv_init();
    lv_tick_set_cb([] { return now_ms; });
    smooth_ui_toolkit::ui_hal::on_get_tick([] { return now_ms; });
    lv_display_t* display = lv_display_create(320, 170);
    testExit();
    testNavigation();
    testTickWrapAndSyntheticRelease();
    lv_display_delete(display);
    lv_deinit();
    std::cout << "ESC exit tests passed\n";
}

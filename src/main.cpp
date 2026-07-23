#include "core/nfc_app.hpp"
#include "hal/nfc_lvgl_hal.hpp"
#include "input/nfc_keypad.hpp"

#include <core/hal/hal.hpp>
#include <lvgl.h>
#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <unistd.h>

int main()
{
    constexpr int32_t kScreenWidth  = 320;
    constexpr int32_t kScreenHeight = 170;

    spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e [%^%l%$] [thread %t] %v");
    spdlog::cfg::load_env_levels();

    lv_init();
    if (!cap_nfc::initLvglHal(kScreenWidth, kScreenHeight)) {
        return 1;
    }

    lv_display_t* display = lv_display_get_default();
    if (!display) {
        std::fprintf(stderr, "Cap-CC1101-NFC: failed to create LVGL display\n");
        cap_nfc::shutdownLvglHal();
        return 1;
    }

    spdlog::info("Cap-CC1101-NFC: display {}x{}", static_cast<int>(lv_display_get_horizontal_resolution(display)),
                 static_cast<int>(lv_display_get_vertical_resolution(display)));
    smooth_ui_toolkit::ui_hal::on_get_tick([]() { return lv_tick_get(); });
    smooth_ui_toolkit::ui_hal::on_delay([](uint32_t milliseconds) { usleep(milliseconds * 1000); });

    cap_nfc::NfcApp app;

#if !LV_USE_SDL
    cap_nfc::NfcKeypad keypad;
    keypad.setKeyCallback(
        [&app](uint32_t key, const char* utf8, bool pressed) { return app.onLvglKeyState(key, utf8, pressed); });
    if (!keypad.openDefault()) {
        spdlog::error("Cap-CC1101-NFC: no usable keyboard input device; aborting startup");
        cap_nfc::shutdownLvglHal();
        return 1;
    }
#endif

    app.start();
    lv_obj_invalidate(lv_screen_active());

    while (!app.quitRequested() && !cap_nfc::lvglHalQuitRequested()) {
#if !LV_USE_SDL
        keypad.poll();
#endif
        lv_timer_handler();
        if (cap_nfc::lvglHalQuitRequested()) {
            break;
        }
        app.tick(lv_tick_get());
        usleep(10000);
    }

    spdlog::info("Cap-CC1101-NFC: exit requested");
    app.stop();
    cap_nfc::shutdownLvglHal();
    return 0;
}

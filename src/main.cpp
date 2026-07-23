#include "core/nfc_app.hpp"
#include "hal/nfc_lvgl_hal.hpp"
#include "input/nfc_keypad.hpp"

#include <core/hal/hal.hpp>
#include <lvgl.h>
#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <csignal>
#include <unistd.h>

namespace {

#if !LV_USE_SDL
constexpr unsigned int kShutdownTimeoutSeconds = 3;
#endif
volatile std::sig_atomic_t g_signal_exit_requested = 0;

void requestExitFromSignal(int signal)
{
    g_signal_exit_requested = signal;
#if !LV_USE_SDL
    // Arm the fallback from the signal handler as well, so it still applies if
    // the main thread is blocked before it reaches the normal shutdown path.
    alarm(kShutdownTimeoutSeconds);
#endif
}

void forceExitAfterShutdownTimeout(int)
{
    constexpr char message[] = "Cap-CC1101-NFC: shutdown timed out; forcing process exit\n";
    const ssize_t ignored    = ::write(STDERR_FILENO, message, sizeof(message) - 1);
    (void)ignored;
    _exit(2);
}

void installSignalHandlers()
{
    struct sigaction action {};
    action.sa_handler = requestExitFromSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);

    action.sa_handler = forceExitAfterShutdownTimeout;
    sigaction(SIGALRM, &action, nullptr);
}

}  // namespace

int main()
{
    constexpr int32_t kScreenWidth  = 320;
    constexpr int32_t kScreenHeight = 170;

    spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e [%^%l%$] [thread %t] %v");
    spdlog::cfg::load_env_levels();
    installSignalHandlers();

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
        keypad.close();
        cap_nfc::shutdownLvglHal();
        return 1;
    }
#endif

    app.start();
    lv_obj_invalidate(lv_screen_active());

    while (!app.quitRequested() && !cap_nfc::lvglHalQuitRequested() && g_signal_exit_requested == 0) {
#if !LV_USE_SDL
        keypad.poll();
        if (app.quitRequested() || g_signal_exit_requested != 0) {
            break;
        }
#endif
        lv_timer_handler();
        if (cap_nfc::lvglHalQuitRequested() || g_signal_exit_requested != 0) {
            break;
        }
        app.tick(lv_tick_get());
        usleep(10000);
    }

    spdlog::info("Cap-CC1101-NFC: exit requested (app={}, display={}, signal={})", app.quitRequested(),
                 cap_nfc::lvglHalQuitRequested(), static_cast<int>(g_signal_exit_requested));
#if !LV_USE_SDL
    alarm(kShutdownTimeoutSeconds);
#endif
    app.stop();
#if !LV_USE_SDL
    keypad.close();
#endif
    spdlog::info("Cap-CC1101-NFC: shutting down display HAL");
    cap_nfc::shutdownLvglHal();
#if !LV_USE_SDL
    alarm(0);
#endif
    spdlog::info("Cap-CC1101-NFC: shutdown complete");
    return 0;
}

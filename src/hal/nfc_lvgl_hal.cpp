#include "hal/nfc_lvgl_hal.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdlib>

#if LV_USE_SDL
#include <SDL2/SDL.h>

#include "src/drivers/sdl/lv_sdl_keyboard.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#elif LV_USE_LINUX_FBDEV
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif

namespace cap_nfc {
namespace {

std::atomic_bool g_quit_requested{false};

const char* envOrDefault(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return value && value[0] != '\0' ? value : fallback;
}

#if LV_USE_SDL
SDL_EventFilter g_previous_event_filter = nullptr;
void* g_previous_event_filter_data      = nullptr;
Uint32 g_window_id                      = 0;

int SDLCALL filterSdlEvent(void* userData, SDL_Event* event)
{
    (void)userData;
    if (event &&
        (event->type == SDL_QUIT || (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_CLOSE &&
                                     event->window.windowID == g_window_id))) {
        g_quit_requested.store(true, std::memory_order_release);
        return 0;
    }
    return g_previous_event_filter ? g_previous_event_filter(g_previous_event_filter_data, event) : 1;
}

float envFloatOrDefault(const char* name, float fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return fallback;
    }

    char* end          = nullptr;
    const float parsed = std::strtof(value, &end);
    return end && end != value && parsed > 0.0f ? parsed : fallback;
}
#endif

}  // namespace

bool initLvglHal(int32_t width, int32_t height)
{
    g_quit_requested.store(false, std::memory_order_release);
#if LV_USE_SDL
    lv_display_t* disp = lv_sdl_window_create(width, height);
    if (!disp) {
        spdlog::error("Cap-CC1101-NFC HAL: failed to create SDL display");
        return false;
    }

    const float zoom = envFloatOrDefault("CAP_NFC_SDL_ZOOM", 1.0f);
    lv_sdl_window_set_resizeable(disp, false);
    lv_sdl_window_set_zoom(disp, zoom);
    lv_sdl_window_set_title(disp, envOrDefault("LV_SDL_WINDOW_TITLE", "Cap-CC1101-NFC"));
    spdlog::info("Cap-CC1101-NFC HAL: SDL logical display {}x{}, zoom {}", width, height, zoom);
    lv_sdl_mouse_create();
    if (!lv_sdl_keyboard_create()) {
        spdlog::error("Cap-CC1101-NFC HAL: failed to create SDL keyboard input");
        lv_display_delete(disp);
        lv_sdl_quit();
        return false;
    }

    g_window_id = SDL_GetWindowID(lv_sdl_window_get_window(disp));
    SDL_GetEventFilter(&g_previous_event_filter, &g_previous_event_filter_data);
    SDL_SetEventFilter(filterSdlEvent, nullptr);
    return true;
#elif LV_USE_LINUX_FBDEV
    (void)width;
    (void)height;
    lv_display_t* disp = lv_linux_fbdev_create();
    if (!disp) {
        spdlog::error("Cap-CC1101-NFC HAL: failed to create framebuffer display");
        return false;
    }

    const char* device = envOrDefault("LV_LINUX_FBDEV_DEVICE", "/dev/fb0");
    if (lv_linux_fbdev_set_file(disp, device) != LV_RESULT_OK) {
        spdlog::error("Cap-CC1101-NFC HAL: failed to open framebuffer {}", device);
        return false;
    }
    return true;
#else
    spdlog::error("Cap-CC1101-NFC HAL: no LVGL display driver enabled");
    return false;
#endif
}

bool lvglHalQuitRequested() noexcept
{
    return g_quit_requested.load(std::memory_order_acquire);
}

void shutdownLvglHal()
{
#if LV_USE_SDL
    SDL_SetEventFilter(g_previous_event_filter, g_previous_event_filter_data);
    g_previous_event_filter      = nullptr;
    g_previous_event_filter_data = nullptr;
    g_window_id                  = 0;
    lv_sdl_quit();
#endif
}

}  // namespace cap_nfc

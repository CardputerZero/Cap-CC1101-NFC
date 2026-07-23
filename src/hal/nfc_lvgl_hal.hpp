#pragma once

#include <lvgl.h>
#include <cstdint>

namespace cap_nfc {

bool initLvglHal(int32_t width, int32_t height);
bool lvglHalQuitRequested() noexcept;
void shutdownLvglHal();

}  // namespace cap_nfc

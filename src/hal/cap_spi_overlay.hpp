#pragma once

#include <atomic>
#include <string>

namespace cap_nfc::hal {

bool ensureCapSpiOverlay(const std::string& expectedDevice, std::string& error,
                         const std::atomic_bool* cancel = nullptr);

}  // namespace cap_nfc::hal

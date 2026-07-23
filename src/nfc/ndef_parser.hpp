// SPDX-License-Identifier: MIT
// Copyright (c) 2026 M5Stack CardputerZero Community

#pragma once

#include "nfc/nfc_types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cap_nfc::nfc {

struct NdefParseResult {
    bool success = false;
    std::string error;
    std::vector<NdefRecord> records;

    explicit operator bool() const noexcept
    {
        return success;
    }
};

NdefParseResult parseNdefMessage(const uint8_t* data, std::size_t size);
NdefParseResult parseNdefMessage(const std::vector<uint8_t>& message);

}  // namespace cap_nfc::nfc

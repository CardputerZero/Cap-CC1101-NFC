#pragma once

#include <cstdint>

namespace cap_nfc {

namespace nfc_key {

constexpr uint32_t Up    = 0x10001;
constexpr uint32_t Down  = 0x10002;
constexpr uint32_t Left  = 0x10003;
constexpr uint32_t Right = 0x10004;

}  // namespace nfc_key

enum class PageId {
    Nfc = 0,
    Count,
};

enum class NfcSection {
    Scan = 0,
    TagDetail,
    ReaderInfo,
    Count,
};

struct ScrollRequest {
    uint32_t serial = 0;
    int32_t amount  = 0;
};

constexpr int32_t kDetailScrollStep = 38;

const char* pageIdName(PageId page);
const char* nfcSectionName(NfcSection section);

}  // namespace cap_nfc

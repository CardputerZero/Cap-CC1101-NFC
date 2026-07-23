#include "core/nfc_types.hpp"

namespace cap_nfc {

const char* pageIdName(PageId page)
{
    switch (page) {
        case PageId::Nfc:
            return "nfc";
        case PageId::Count:
            break;
    }
    return "unknown";
}

const char* nfcSectionName(NfcSection section)
{
    switch (section) {
        case NfcSection::Scan:
            return "scan";
        case NfcSection::TagDetail:
            return "tag-detail";
        case NfcSection::ReaderInfo:
            return "reader-info";
        case NfcSection::Count:
            break;
    }
    return "unknown";
}

}  // namespace cap_nfc

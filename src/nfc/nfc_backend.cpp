#include "nfc/nfc_backend.hpp"

#include <stdexcept>

namespace cap_nfc::nfc {

std::unique_ptr<NfcBackend> makeDefaultNfcBackend()
{
#if defined(CAP_NFC_USE_MOCK_BACKEND) && CAP_NFC_USE_MOCK_BACKEND
    return makeMockNfcBackend();
#elif defined(CAP_NFC_USE_LINUX_BACKEND) && CAP_NFC_USE_LINUX_BACKEND
    return makeLinuxNfcBackend();
#elif defined(CAP_NFC_USE_UNAVAILABLE_BACKEND) && CAP_NFC_USE_UNAVAILABLE_BACKEND
    return makeUnavailableNfcBackend();
#else
    throw std::runtime_error("no NFC backend selected");
#endif
}

}  // namespace cap_nfc::nfc

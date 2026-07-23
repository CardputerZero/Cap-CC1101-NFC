#pragma once

#include "nfc/nfc_types.hpp"

#include <chrono>
#include <memory>

namespace cap_nfc::nfc {

class NfcBackend {
public:
    virtual ~NfcBackend() = default;

    NfcBackend(const NfcBackend&)            = delete;
    NfcBackend& operator=(const NfcBackend&) = delete;

    virtual ReaderInfo open(const CancellationToken& cancellation) = 0;
    virtual void close() noexcept                                  = 0;
    virtual void closeImmediately() noexcept
    {
        close();
    }
    virtual void startDiscovery(const CancellationToken& cancellation)               = 0;
    virtual void stopDiscovery() noexcept                                            = 0;
    virtual DiscoveryPollResult pollDiscovery(std::chrono::milliseconds timeout,
                                              const CancellationToken& cancellation) = 0;

protected:
    NfcBackend() = default;
};

std::unique_ptr<NfcBackend> makeMockNfcBackend();
std::unique_ptr<NfcBackend> makeLinuxNfcBackend();
std::unique_ptr<NfcBackend> makeUnavailableNfcBackend();
std::unique_ptr<NfcBackend> makeDefaultNfcBackend();

}  // namespace cap_nfc::nfc

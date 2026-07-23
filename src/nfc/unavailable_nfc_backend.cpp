#include "nfc/nfc_backend.hpp"

namespace cap_nfc::nfc {
namespace {

class UnavailableNfcBackend final : public NfcBackend {
public:
    ReaderInfo open(const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        throw NfcBackendUnavailable("ST25R3916 Linux backend is not available in this build");
    }

    void close() noexcept override
    {
    }

    void startDiscovery(const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        throw NfcBackendUnavailable("ST25R3916 Linux backend is not available in this build");
    }

    void stopDiscovery() noexcept override
    {
    }

    DiscoveryPollResult pollDiscovery(std::chrono::milliseconds timeout, const CancellationToken& cancellation) override
    {
        (void)timeout;
        cancellation.throwIfCancellationRequested();
        throw NfcBackendUnavailable("ST25R3916 Linux backend is not available in this build");
    }
};

}  // namespace

std::unique_ptr<NfcBackend> makeUnavailableNfcBackend()
{
    return std::make_unique<UnavailableNfcBackend>();
}

}  // namespace cap_nfc::nfc

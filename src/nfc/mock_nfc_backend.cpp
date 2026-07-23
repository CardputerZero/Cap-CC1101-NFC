#include "nfc/nfc_backend.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>

namespace cap_nfc::nfc {
namespace {

using Clock = std::chrono::steady_clock;

long envLong(const char* name, long fallback, long minimum, long maximum)
{
    const char* text = std::getenv(name);
    if (!text || text[0] == '\0') {
        return fallback;
    }

    char* end        = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || end == text || *end != '\0') {
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

std::string envString(const char* name, const char* fallback)
{
    const char* text = std::getenv(name);
    return text && text[0] != '\0' ? text : fallback;
}

void cancellableSleep(std::chrono::milliseconds duration, const CancellationToken& cancellation)
{
    const auto deadline = Clock::now() + std::max(duration, std::chrono::milliseconds::zero());
    while (Clock::now() < deadline) {
        cancellation.throwIfCancellationRequested();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        std::this_thread::sleep_for(std::min(remaining, std::chrono::milliseconds(10)));
    }
    cancellation.throwIfCancellationRequested();
}

TagSnapshot makeTextTag()
{
    TagSnapshot tag;
    tag.technology    = TagTechnology::NfcA;
    tag.uid           = {0x04, 0xA1, 0x6B, 0x7C, 0x92, 0x64, 0x80};
    tag.atqa          = {0x00, 0x44};
    tag.sak           = 0x00;
    tag.typeName      = "Type 2 / NTAG-compatible";
    tag.ndefSupported = true;
    tag.ndefReadable  = true;
    tag.ndefCapacity  = 144;
    tag.records.push_back(NdefRecord{NdefRecordKind::Text, "text/plain", "Cardputer Zero NFC demo", {'e', 'n'}});
    return tag;
}

TagSnapshot makeUriTag()
{
    TagSnapshot tag;
    tag.technology    = TagTechnology::NfcA;
    tag.uid           = {0x04, 0x73, 0x2B, 0x1A, 0x9C, 0x72, 0x81};
    tag.atqa          = {0x03, 0x44};
    tag.sak           = 0x20;
    tag.typeName      = "Type 4 / ISO-DEP";
    tag.ndefSupported = true;
    tag.ndefReadable  = true;
    tag.ndefCapacity  = 512;
    tag.records.push_back(NdefRecord{NdefRecordKind::Uri, "text/uri-list", "https://m5stack.com", {0x04}});
    return tag;
}

class MockNfcBackend final : public NfcBackend {
public:
    ReaderInfo open(const CancellationToken& cancellation) override
    {
        close();
        ++_open_attempts;
        cancellableSleep(std::chrono::milliseconds(250), cancellation);

        const auto failures = static_cast<std::size_t>(envLong("NFC_MOCK_INIT_FAIL_COUNT", 0, 0, 1000));
        if (_open_attempts <= failures) {
            throw std::runtime_error("mock initialization failure requested by NFC_MOCK_INIT_FAIL_COUNT");
        }

        _scenario = envString("NFC_MOCK_SCENARIO", "cycle");
        _opened   = true;
        _openedAt = Clock::now();

        ReaderInfo info;
        info.backendName = "SDL mock";
        info.chipName    = "ST25R3916";
        info.chipVersion = "mock v1";
        info.transport   = "/dev/spidev0.0 (simulated)";
        info.irq         = "simulated";
        info.power       = "EXT5V + G26 (simulated)";
        info.protocols   = "NFC-A / B / F / V";
        info.mock        = true;
        return info;
    }

    void close() noexcept override
    {
        _opened   = false;
        _scanning = false;
    }

    void startDiscovery(const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        requireOpen();
        _scanning = true;
    }

    void stopDiscovery() noexcept override
    {
        _scanning = false;
    }

    DiscoveryPollResult pollDiscovery(std::chrono::milliseconds timeout, const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        requireOpen();
        if (!_scanning) {
            return {};
        }

        cancellableSleep(std::min(timeout, std::chrono::milliseconds(40)), cancellation);
        if (_scenario == "empty") {
            return {DiscoveryPollKind::NoTag, std::nullopt};
        }
        if (_scenario == "text") {
            return {DiscoveryPollKind::Tag, makeTextTag()};
        }
        if (_scenario == "uri") {
            return {DiscoveryPollKind::Tag, makeUriTag()};
        }

        constexpr int64_t kCycleMs = 15000;
        const auto elapsed  = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - _openedAt).count();
        const int64_t phase = elapsed % kCycleMs;
        if (phase >= 2000 && phase < 7500) {
            return {DiscoveryPollKind::Tag, makeTextTag()};
        }
        if (phase >= 9500 && phase < 13500) {
            return {DiscoveryPollKind::Tag, makeUriTag()};
        }
        return {DiscoveryPollKind::NoTag, std::nullopt};
    }

private:
    std::size_t _open_attempts = 0;
    bool _opened               = false;
    bool _scanning             = false;
    std::string _scenario{"cycle"};
    Clock::time_point _openedAt{};

    void requireOpen() const
    {
        if (!_opened) {
            throw std::runtime_error("mock NFC reader is not open");
        }
    }
};

}  // namespace

std::unique_ptr<NfcBackend> makeMockNfcBackend()
{
    return std::make_unique<MockNfcBackend>();
}

}  // namespace cap_nfc::nfc

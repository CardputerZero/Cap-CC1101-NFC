#include "nfc/nfc_backend.hpp"

#if !defined(__linux__)
#error "The ST25R3916 Linux backend requires Linux"
#endif

#include "hal/cap_spi_overlay.hpp"
#include "hal/cardputerzero_cap_power.hpp"
#include "hal/linux_gpio_line.hpp"
#include "hal/linux_spi_device.hpp"
#include "nfc/ndef_parser.hpp"
#include "nfc/st25r3916_driver.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace cap_nfc::nfc {
namespace {

using namespace std::chrono_literals;

struct LinuxBackendConfig {
    std::string spiDevice{"/dev/spidev0.2"};
    uint32_t spiSpeedHz = 5'000'000;
    std::string gpioChip{"/dev/gpiochip0"};
    unsigned int irqGpio      = 23;
    unsigned int misoPullMode = 3;
};

std::string envString(const char* name, std::string fallback)
{
    const char* value = std::getenv(name);
    return value && value[0] != '\0' ? value : std::move(fallback);
}

unsigned long envUnsigned(const char* name, unsigned long fallback, unsigned long minimum, unsigned long maximum)
{
    const char* text = std::getenv(name);
    if (!text || text[0] == '\0') {
        return fallback;
    }

    char* end                 = nullptr;
    errno                     = 0;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < minimum || value > maximum) {
        throw std::runtime_error(std::string("invalid ") + name + "='" + text + "' (expected " +
                                 std::to_string(minimum) + ".." + std::to_string(maximum) + ")");
    }
    return value;
}

LinuxBackendConfig loadConfig()
{
    LinuxBackendConfig config;
    config.spiSpeedHz =
        static_cast<uint32_t>(envUnsigned("CAP_NFC_SPI_SPEED_HZ", config.spiSpeedHz, 100'000, 10'000'000));
    config.gpioChip     = envString("CAP_NFC_GPIO_CHIP", config.gpioChip);
    config.irqGpio      = static_cast<unsigned int>(envUnsigned("CAP_NFC_IRQ_GPIO", config.irqGpio, 0, 1023));
    config.misoPullMode = static_cast<unsigned int>(envUnsigned("CAP_NFC_MISO_PULL_MODE", config.misoPullMode, 0, 3));
    return config;
}

std::string hexByte(uint8_t value)
{
    constexpr char digits[] = "0123456789ABCDEF";
    std::string text{"0x00"};
    text[2] = digits[value >> 4];
    text[3] = digits[value & 0x0F];
    return text;
}

void cancellableSleep(std::chrono::milliseconds duration, const CancellationToken& cancellation)
{
    const auto deadline = std::chrono::steady_clock::now() + std::max(duration, 0ms);
    while (std::chrono::steady_clock::now() < deadline) {
        cancellation.throwIfCancellationRequested();
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        std::this_thread::sleep_for(std::max(1ms, std::min(remaining, 10ms)));
    }
    cancellation.throwIfCancellationRequested();
}

class LinuxSt25r3916Transport final : public St25r3916Transport {
public:
    LinuxSt25r3916Transport(hal::LinuxSpiDevice& spi, hal::LinuxGpioLine& interrupt) : _spi(spi), _interrupt(interrupt)
    {
    }

    void transfer(const uint8_t* transmit, uint8_t* receive, std::size_t size) override
    {
        _spi.transfer(transmit, receive, size);
    }

    bool interruptAsserted() const override
    {
        return _interrupt.value();
    }

    bool waitForInterrupt(std::chrono::milliseconds timeout, const CancellationToken& cancellation) override
    {
        const auto count  = std::clamp<int64_t>(timeout.count(), 0, INT_MAX);
        const auto result = _interrupt.waitForRisingEdge(static_cast<int>(count), cancellation.nativeFlag());
        if (result == hal::LinuxGpioLine::WaitResult::Cancelled) {
            throw NfcCancelled{};
        }
        return result == hal::LinuxGpioLine::WaitResult::RisingEdge;
    }

    void sleepFor(std::chrono::milliseconds duration, const CancellationToken& cancellation) override
    {
        cancellableSleep(duration, cancellation);
    }

private:
    hal::LinuxSpiDevice& _spi;
    hal::LinuxGpioLine& _interrupt;
};

class LinuxNfcBackend final : public NfcBackend {
public:
    ReaderInfo open(const CancellationToken& cancellation) override
    {
        close();
        std::string_view stage = "configuration";
        try {
            _config = loadConfig();
            spdlog::info(
                "NFC backend: topology SPI={} mode=1 speed={} Hz kernel-CS2(GPIO22), IRQ={}:{} rising-edge, "
                "MISO-pull-mode={}",
                _config.spiDevice, _config.spiSpeedHz, _config.gpioChip, _config.irqGpio, _config.misoPullMode);

            stage = "SPI overlay load";
            std::string overlayError;
            if (!hal::ensureCapSpiOverlay(_config.spiDevice, overlayError, cancellation.nativeFlag())) {
                cancellation.throwIfCancellationRequested();
                throw std::runtime_error("Cap SPI overlay unavailable: " + overlayError);
            }

            stage      = "GPIO setup";
            _interrupt = std::make_unique<hal::LinuxGpioLine>(_config.gpioChip, _config.irqGpio);
            _interrupt->requestRisingEdge();
            spdlog::info("NFC backend: kernel-managed CS2 active; IRQ line {}:{} armed", _config.gpioChip,
                         _config.irqGpio);

            stage = "Cap power enable";
            spdlog::info("NFC backend: enabling Cap power (G26 POWER_EN + ext_5v_out LED class)");
            std::string powerError;
            if (!_power.enable(powerError, cancellation.nativeFlag())) {
                cancellation.throwIfCancellationRequested();
                throw std::runtime_error("Cap power enable failed: " + powerError);
            }
            cancellableSleep(100ms, cancellation);

            stage = "SPI open";
            hal::LinuxSpiDevice::Config spiConfig;
            spiConfig.path          = _config.spiDevice;
            spiConfig.speed_hz      = _config.spiSpeedHz;
            spiConfig.mode          = 1;
            spiConfig.bits_per_word = 8;
            spiConfig.no_kernel_cs  = false;
            _spi.open(spiConfig);

            stage      = "ST25R3916 probe and NFC-A/F setup";
            _transport = std::make_unique<LinuxSt25r3916Transport>(_spi, *_interrupt);
            St25r3916DriverConfig driverConfig;
            driverConfig.misoPullWhenDeselected = (_config.misoPullMode & 0x01) != 0;
            driverConfig.misoPullWhenSelected   = (_config.misoPullMode & 0x02) != 0;
            _driver                             = std::make_unique<St25r3916Driver>(*_transport, driverConfig);
            const St25r3916ChipInfo chip        = _driver->initialize(cancellation);

            ReaderInfo info;
            info.backendName = "ST25R3916 Linux";
            info.chipName    = "ST25R3916";
            info.chipVersion = "ID " + hexByte(chip.identity) + " / rev " + std::to_string(chip.revision);
            info.transport =
                _config.spiDevice + " mode 1 @ " + std::to_string(_config.spiSpeedHz) + " Hz / kernel CS2 GPIO22";
            info.irq       = _config.gpioChip + ":" + std::to_string(_config.irqGpio) + " rising edge";
            info.power     = "ext_5v_out + GPIO26 POWER_EN";
            info.protocols = "NFC-A / NFC-F UID; Type 2 NDEF for NFC-A";
            info.mock      = false;
            _open          = true;

            spdlog::info(
                "NFC backend: open complete (identity={}, revision={}, VDD_ADC={}, regulator={}, protocols={})",
                hexByte(chip.identity), chip.revision, hexByte(chip.measuredSupply), hexByte(chip.regulatorDisplay),
                info.protocols);
            return info;
        } catch (const NfcCancelled&) {
            closeImmediately();
            throw;
        } catch (const std::exception& exception) {
            const std::string message =
                "ST25R3916 initialization failed at " + std::string(stage) + ": " + exception.what();
            spdlog::error("{}", message);
            closeImmediately();
            throw std::runtime_error(message);
        }
    }

    void close() noexcept override
    {
        closeImpl();
    }

    void startDiscovery(const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        requireOpen();
        // Start in FeliCa mode so a phone that exposes more than one NFC
        // technology is classified by its NFC-F response first.
        _driver->startNfcF(cancellation);
        _probe_protocol          = DiscoveryProtocol::NfcF;
        _locked_protocol         = LockedProtocol::None;
        _locked_no_tag_count     = 0;
        _nfcf_clean_no_tag_count = 0;
        _nfcf_inconclusive_count = 0;
        _scanning                = true;
        spdlog::info("NFC backend: NFC-F/NFC-A discovery active (FeliCa preferred)");
    }

    void stopDiscovery() noexcept override
    {
        if (_driver) {
            _driver->stopNfcA();
        }
        _scanning                = false;
        _probe_protocol          = DiscoveryProtocol::NfcF;
        _locked_protocol         = LockedProtocol::None;
        _locked_no_tag_count     = 0;
        _nfcf_clean_no_tag_count = 0;
        _nfcf_inconclusive_count = 0;
        spdlog::info("NFC backend: NFC-A/NFC-F discovery stopped and RF field disabled");
    }

    DiscoveryPollResult pollDiscovery(std::chrono::milliseconds timeout, const CancellationToken& cancellation) override
    {
        (void)timeout;
        cancellation.throwIfCancellationRequested();
        requireOpen();
        if (!_scanning) {
            return {};
        }

        const auto pollF = [&]() {
            const auto observed = _driver->pollNfcF(cancellation);
            if (observed.kind == St25r3916NfcFPollKind::Tag && observed.tag) {
                const St25r3916NfcFTag& tag = *observed.tag;
                TagSnapshot snapshot;
                snapshot.technology = TagTechnology::NfcF;
                snapshot.uid        = {tag.idm.begin(), tag.idm.end()};
                snapshot.typeName   = tag.typeName;
                spdlog::debug("NFC backend: NFC-F tag detected (IDm={})", bytesToHex(snapshot.uid));
                return DiscoveryPollResult{DiscoveryPollKind::Tag, std::move(snapshot)};
            }
            if (observed.kind == St25r3916NfcFPollKind::NoTag) {
                return DiscoveryPollResult{DiscoveryPollKind::NoTag, std::nullopt};
            }
            return DiscoveryPollResult{DiscoveryPollKind::NoObservation, std::nullopt};
        };

        const auto pollA = [&]() {
            auto observed = _driver->pollNfcA(cancellation);
            if (observed.kind == St25r3916NfcAPollKind::Tag && observed.tag) {
                St25r3916NfcATag& tag = *observed.tag;
                TagSnapshot snapshot;
                snapshot.technology    = TagTechnology::NfcA;
                snapshot.uid           = std::move(tag.uid);
                snapshot.atqa          = {tag.atqa.begin(), tag.atqa.end()};
                snapshot.sak           = tag.sak;
                snapshot.typeName      = std::move(tag.typeName);
                snapshot.ndefSupported = tag.ndefSupported;
                snapshot.ndefReadable  = tag.ndefReadable;
                snapshot.ndefCapacity  = tag.ndefCapacity;
                if (!tag.ndefMessage.empty()) {
                    NdefParseResult parsed = parseNdefMessage(tag.ndefMessage);
                    if (parsed) {
                        snapshot.records = std::move(parsed.records);
                    } else {
                        spdlog::warn("NFC backend: NDEF bytes from UID {} could not be parsed: {}",
                                     bytesToHex(snapshot.uid), parsed.error);
                    }
                }
                spdlog::debug("NFC backend: NFC-A tag detected (UID={})", bytesToHex(snapshot.uid));
                return DiscoveryPollResult{DiscoveryPollKind::Tag, std::move(snapshot)};
            }
            if (observed.kind == St25r3916NfcAPollKind::NoTag) {
                return DiscoveryPollResult{DiscoveryPollKind::NoTag, std::nullopt};
            }
            return DiscoveryPollResult{DiscoveryPollKind::NoObservation, std::nullopt};
        };

        const auto pollProtocol = [&](DiscoveryProtocol protocol) {
            return protocol == DiscoveryProtocol::NfcF ? pollF() : pollA();
        };

        const auto lockProtocol = [&](DiscoveryProtocol protocol, DiscoveryPollResult result) {
            _locked_protocol     = protocol == DiscoveryProtocol::NfcF ? LockedProtocol::NfcF : LockedProtocol::NfcA;
            _probe_protocol      = protocol;
            _locked_no_tag_count = 0;
            _nfcf_clean_no_tag_count = 0;
            _nfcf_inconclusive_count = 0;
            return result;
        };

        // A bounded escape hatch for a noisy FeliCa exchange. One or two
        // incomplete responses are common on translated SPI/IRQ buses, so F
        // remains preferred for those polls. Repeated incompletes can also be
        // caused by an NFC-A card or a stale field, however; probe A once so
        // that an NFC-A tag cannot be hidden behind a permanent F lock.
        const auto fallbackToA = [&]() {
            const DiscoveryPollResult result = pollA();
            if (result.kind == DiscoveryPollKind::Tag) {
                return lockProtocol(DiscoveryProtocol::NfcA, result);
            }
            _probe_protocol          = DiscoveryProtocol::NfcF;
            _locked_no_tag_count     = 0;
            _nfcf_clean_no_tag_count = 0;
            _nfcf_inconclusive_count = 0;
            return result;
        };

        // Keep the selected protocol while a tag remains present. Switching
        // modes on every cycle can interrupt Type 2 reads and makes a card
        // appear to flicker between technologies.
        if (_locked_protocol != LockedProtocol::None) {
            const DiscoveryProtocol protocol =
                _locked_protocol == LockedProtocol::NfcF ? DiscoveryProtocol::NfcF : DiscoveryProtocol::NfcA;
            const DiscoveryPollResult result = pollProtocol(protocol);
            if (result.kind == DiscoveryPollKind::Tag) {
                _locked_no_tag_count     = 0;
                _nfcf_clean_no_tag_count = 0;
                _nfcf_inconclusive_count = 0;
                return result;
            }
            if (result.kind == DiscoveryPollKind::NoObservation) {
                _locked_no_tag_count     = 0;
                _nfcf_clean_no_tag_count = 0;
                if (protocol == DiscoveryProtocol::NfcF) {
                    ++_nfcf_inconclusive_count;
                    if (_nfcf_inconclusive_count >= kNfcFInconclusiveFallbackThreshold) {
                        spdlog::warn(
                            "NFC backend: locked NFC-F produced {} consecutive incomplete polls; probing NFC-A",
                            _nfcf_inconclusive_count);
                        _locked_protocol = LockedProtocol::None;
                        return fallbackToA();
                    }
                }
                // A noisy/incomplete exchange does not prove that the tag
                // was removed. Keep the protocol lock and restart the clean
                // absence debounce window until the bounded fallback above.
                return result;
            }
            if (result.kind == DiscoveryPollKind::NoTag) {
                _nfcf_inconclusive_count = 0;
                ++_locked_no_tag_count;
                if (_locked_no_tag_count < kLockedNoTagThreshold) {
                    spdlog::debug("NFC backend: locked {} poll reports no tag ({}/{}); retaining protocol lock",
                                  protocolName(protocol), _locked_no_tag_count, kLockedNoTagThreshold);
                    return result;
                }
                spdlog::debug("NFC backend: locked {} protocol released after {} clean no-tag polls",
                              protocolName(protocol), kLockedNoTagThreshold);
                _locked_protocol         = LockedProtocol::None;
                _probe_protocol          = DiscoveryProtocol::NfcF;
                _locked_no_tag_count     = 0;
                _nfcf_clean_no_tag_count = 0;
                _nfcf_inconclusive_count = 0;
            }
            return result;
        }

        const DiscoveryProtocol first = _probe_protocol;
        const DiscoveryProtocol second =
            first == DiscoveryProtocol::NfcF ? DiscoveryProtocol::NfcA : DiscoveryProtocol::NfcF;
        const DiscoveryPollResult firstResult = pollProtocol(first);
        if (firstResult.kind == DiscoveryPollKind::Tag) {
            return lockProtocol(first, firstResult);
        }

        // A partial/colliding NFC-F exchange is evidence that a tag is in the
        // field, but not enough evidence to publish an IDm. Do not immediately
        // run NFC-A in that case: an NFC-F phone can otherwise be misreported
        // as NFC-A when its FeliCa response is received on the next edge.
        if (first == DiscoveryProtocol::NfcF && firstResult.kind == DiscoveryPollKind::NoObservation) {
            ++_nfcf_inconclusive_count;
            if (_nfcf_inconclusive_count < kNfcFInconclusiveFallbackThreshold) {
                _probe_protocol          = DiscoveryProtocol::NfcF;
                _nfcf_clean_no_tag_count = 0;
                return firstResult;
            }
            spdlog::warn("NFC backend: NFC-F produced {} consecutive incomplete polls; probing NFC-A",
                         _nfcf_inconclusive_count);
            return fallbackToA();
        }

        // Require two clean NFC-F no-response polls before probing NFC-A.
        // A FeliCa phone can answer just after the first timeout; switching
        // immediately would let a noisy NFC-A interpretation win that race.
        if (first == DiscoveryProtocol::NfcF && firstResult.kind == DiscoveryPollKind::NoTag) {
            _nfcf_inconclusive_count = 0;
            ++_nfcf_clean_no_tag_count;
            if (_nfcf_clean_no_tag_count < kNfcFCleanNoTagThreshold) {
                spdlog::debug("NFC backend: NFC-F clean no-tag poll ({}/{}); delaying NFC-A probe",
                              _nfcf_clean_no_tag_count, kNfcFCleanNoTagThreshold);
                return firstResult;
            }
        }

        // A clean NFC-F no-response is the only case in which it is safe to
        // try the other mode. Receive activity was handled above so a noisy
        // FeliCa exchange cannot hide an NFC-F phone as NFC-A.
        const DiscoveryPollResult secondResult = pollProtocol(second);
        if (secondResult.kind == DiscoveryPollKind::Tag) {
            return lockProtocol(second, secondResult);
        }

        _probe_protocol          = DiscoveryProtocol::NfcF;
        _locked_no_tag_count     = 0;
        _nfcf_clean_no_tag_count = 0;
        _nfcf_inconclusive_count = 0;
        if (firstResult.kind == DiscoveryPollKind::NoObservation ||
            secondResult.kind == DiscoveryPollKind::NoObservation) {
            return {DiscoveryPollKind::NoObservation, std::nullopt};
        }
        return {DiscoveryPollKind::NoTag, std::nullopt};
    }

private:
    static constexpr std::size_t kLockedNoTagThreshold              = 3;
    static constexpr std::size_t kNfcFCleanNoTagThreshold           = 2;
    static constexpr std::size_t kNfcFInconclusiveFallbackThreshold = 3;

    enum class DiscoveryProtocol {
        NfcF,
        NfcA,
    };

    enum class LockedProtocol {
        None,
        NfcF,
        NfcA,
    };

    LinuxBackendConfig _config;
    hal::CardputerZeroCapPower _power;
    hal::LinuxSpiDevice _spi;
    std::unique_ptr<hal::LinuxGpioLine> _interrupt;
    std::unique_ptr<LinuxSt25r3916Transport> _transport;
    std::unique_ptr<St25r3916Driver> _driver;
    bool _open                           = false;
    bool _scanning                       = false;
    DiscoveryProtocol _probe_protocol    = DiscoveryProtocol::NfcF;
    LockedProtocol _locked_protocol      = LockedProtocol::None;
    std::size_t _locked_no_tag_count     = 0;
    std::size_t _nfcf_clean_no_tag_count = 0;
    std::size_t _nfcf_inconclusive_count = 0;

    static const char* protocolName(DiscoveryProtocol protocol) noexcept
    {
        return protocol == DiscoveryProtocol::NfcF ? "NFC-F" : "NFC-A";
    }

    void closeImpl() noexcept
    {
        const bool hadResources = _open || _driver || _transport || _spi.isOpen() || _interrupt || _power.enabled();
        if (hadResources) {
            spdlog::info("NFC backend: closing resources");
        }

        _scanning                = false;
        _probe_protocol          = DiscoveryProtocol::NfcF;
        _locked_protocol         = LockedProtocol::None;
        _locked_no_tag_count     = 0;
        _nfcf_clean_no_tag_count = 0;
        _nfcf_inconclusive_count = 0;
        if (_driver) {
            // Error/cancellation cleanup must also stop RF now that I/O power
            // stays on. stop() issues commands without IRQ or discovery waits.
            spdlog::info("NFC backend: disabling RF field through ST25R3916");
            _driver->stop();
            _driver.reset();
        }
        _transport.reset();
        if (_spi.isOpen()) {
            spdlog::info("NFC backend: closing SPI device");
        }
        _spi.close();
        if (_interrupt) {
            spdlog::info("NFC backend: releasing IRQ GPIO");
            _interrupt->release();
            _interrupt.reset();
        }
        if (_power.enabled()) {
            spdlog::info("NFC backend: releasing Cap controls while retaining shared SPI power");
        }
        _power.disable();
        _open = false;
        if (hadResources) {
            spdlog::info("NFC backend: resources closed");
        }
    }

    void requireOpen() const
    {
        if (!_open || !_driver || !_driver->ready()) {
            throw std::runtime_error("ST25R3916 Linux backend is not open");
        }
    }
};

}  // namespace

std::unique_ptr<NfcBackend> makeLinuxNfcBackend()
{
    return std::make_unique<LinuxNfcBackend>();
}

}  // namespace cap_nfc::nfc

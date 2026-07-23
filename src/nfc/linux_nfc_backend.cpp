#include "nfc/nfc_backend.hpp"

#if !defined(__linux__)
#error "The ST25R3916 Linux backend requires Linux"
#endif

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
    std::string spiDevice{"/dev/spidev0.1"};
    uint32_t spiSpeedHz = 5'000'000;
    std::string gpioChip{"/dev/gpiochip0"};
    unsigned int csGpio       = 22;
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
    config.spiDevice = envString("CAP_NFC_SPI_DEVICE", config.spiDevice);
    config.spiSpeedHz =
        static_cast<uint32_t>(envUnsigned("CAP_NFC_SPI_SPEED_HZ", config.spiSpeedHz, 100'000, 10'000'000));
    config.gpioChip     = envString("CAP_NFC_GPIO_CHIP", config.gpioChip);
    config.csGpio       = static_cast<unsigned int>(envUnsigned("CAP_NFC_CS_GPIO", config.csGpio, 0, 1023));
    config.irqGpio      = static_cast<unsigned int>(envUnsigned("CAP_NFC_IRQ_GPIO", config.irqGpio, 0, 1023));
    config.misoPullMode = static_cast<unsigned int>(envUnsigned("CAP_NFC_MISO_PULL_MODE", config.misoPullMode, 0, 3));
    if (config.csGpio == config.irqGpio) {
        throw std::runtime_error("CAP_NFC_CS_GPIO and CAP_NFC_IRQ_GPIO must be different lines");
    }
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
    LinuxSt25r3916Transport(hal::LinuxSpiDevice& spi, hal::LinuxGpioLine& chipSelect, hal::LinuxGpioLine& interrupt)
        : _spi(spi), _chip_select(chipSelect), _interrupt(interrupt)
    {
    }

    void transfer(const uint8_t* transmit, uint8_t* receive, std::size_t size) override
    {
        _chip_select.setValue(false);
        try {
            _spi.transfer(transmit, receive, size);
        } catch (...) {
            try {
                _chip_select.setValue(true);
            } catch (const std::exception& exception) {
                spdlog::error("ST25R3916 transport: failed to release CS after SPI error: {}", exception.what());
            }
            throw;
        }
        _chip_select.setValue(true);
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
    hal::LinuxGpioLine& _chip_select;
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
                "NFC backend: topology SPI={} mode=1 speed={} Hz no-kernel-CS, CS={}:{}, IRQ={}:{} rising-edge, "
                "MISO-pull-mode={}",
                _config.spiDevice, _config.spiSpeedHz, _config.gpioChip, _config.csGpio, _config.gpioChip,
                _config.irqGpio, _config.misoPullMode);
            spdlog::warn(
                "NFC backend: GPIO{} is a userspace chip-select; if this SPI controller is shared, request a "
                "kernel-managed CS/spidev node from the BSP to make transfers atomic",
                _config.csGpio);

            stage        = "GPIO setup";
            _chip_select = std::make_unique<hal::LinuxGpioLine>(_config.gpioChip, _config.csGpio);
            _interrupt   = std::make_unique<hal::LinuxGpioLine>(_config.gpioChip, _config.irqGpio);
            _chip_select->requestOutput(true);
            _interrupt->requestRisingEdge();
            spdlog::info("NFC backend: manual CS line {}:{} held high; IRQ line {}:{} armed", _config.gpioChip,
                         _config.csGpio, _config.gpioChip, _config.irqGpio);

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
            spiConfig.no_kernel_cs  = true;
            _spi.open(spiConfig);

            stage      = "ST25R3916 probe and NFC-A setup";
            _transport = std::make_unique<LinuxSt25r3916Transport>(_spi, *_chip_select, *_interrupt);
            St25r3916DriverConfig driverConfig;
            driverConfig.misoPullWhenDeselected = (_config.misoPullMode & 0x01) != 0;
            driverConfig.misoPullWhenSelected   = (_config.misoPullMode & 0x02) != 0;
            _driver                             = std::make_unique<St25r3916Driver>(*_transport, driverConfig);
            const St25r3916ChipInfo chip        = _driver->initialize(cancellation);

            ReaderInfo info;
            info.backendName = "ST25R3916 Linux";
            info.chipName    = "ST25R3916";
            info.chipVersion = "ID " + hexByte(chip.identity) + " / rev " + std::to_string(chip.revision);
            info.transport   = _config.spiDevice + " mode 1 @ " + std::to_string(_config.spiSpeedHz) +
                             " Hz / manual CS GPIO" + std::to_string(_config.csGpio);
            info.irq       = _config.gpioChip + ":" + std::to_string(_config.irqGpio) + " rising edge";
            info.power     = "ext_5v_out + GPIO26 POWER_EN";
            info.protocols = "NFC-A UID / Type 2 NDEF (bring-up)";
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
        closeImpl(true);
    }

    void closeImmediately() noexcept override
    {
        closeImpl(false);
    }

    void startDiscovery(const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        requireOpen();
        _driver->startNfcA(cancellation);
        _scanning = true;
        spdlog::info("NFC backend: NFC-A discovery active");
    }

    void stopDiscovery() noexcept override
    {
        if (_driver) {
            _driver->stopNfcA();
        }
        _scanning = false;
        spdlog::info("NFC backend: NFC-A discovery stopped and RF field disabled");
    }

    DiscoveryPollResult pollDiscovery(std::chrono::milliseconds timeout, const CancellationToken& cancellation) override
    {
        (void)timeout;
        cancellation.throwIfCancellationRequested();
        requireOpen();
        if (!_scanning) {
            return {};
        }

        auto observed = _driver->pollNfcA(cancellation);
        if (observed.kind == St25r3916NfcAPollKind::NoTag) {
            return {DiscoveryPollKind::NoTag, std::nullopt};
        }
        if (observed.kind != St25r3916NfcAPollKind::Tag || !observed.tag) {
            return {DiscoveryPollKind::NoObservation, std::nullopt};
        }

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
                spdlog::warn("NFC backend: NDEF bytes from UID {} could not be parsed: {}", bytesToHex(snapshot.uid),
                             parsed.error);
            }
        }
        return {DiscoveryPollKind::Tag, std::move(snapshot)};
    }

private:
    LinuxBackendConfig _config;
    hal::CardputerZeroCapPower _power;
    hal::LinuxSpiDevice _spi;
    std::unique_ptr<hal::LinuxGpioLine> _chip_select;
    std::unique_ptr<hal::LinuxGpioLine> _interrupt;
    std::unique_ptr<LinuxSt25r3916Transport> _transport;
    std::unique_ptr<St25r3916Driver> _driver;
    bool _open     = false;
    bool _scanning = false;

    void closeImpl(bool graceful) noexcept
    {
        const bool hadResources =
            _open || _driver || _transport || _spi.isOpen() || _interrupt || _chip_select || _power.enabled();
        if (hadResources) {
            spdlog::info("NFC backend: closing resources (mode={})", graceful ? "graceful" : "immediate");
        }

        _scanning = false;
        if (_driver) {
            if (graceful) {
                spdlog::info("NFC backend: disabling RF field through ST25R3916");
                _driver->stop();
            }
            _driver.reset();
        }
        _transport.reset();
        if (_chip_select) {
            spdlog::info("NFC backend: returning software CS high");
            try {
                _chip_select->setValue(true);
            } catch (const std::exception& exception) {
                spdlog::warn("NFC backend: failed to leave CS high during close: {}", exception.what());
            }
        }
        if (_spi.isOpen()) {
            spdlog::info("NFC backend: closing SPI device");
        }
        _spi.close();
        if (_interrupt) {
            spdlog::info("NFC backend: releasing IRQ GPIO");
            _interrupt->release();
            _interrupt.reset();
        }
        if (_chip_select) {
            spdlog::info("NFC backend: releasing software CS GPIO");
            _chip_select->release();
            _chip_select.reset();
        }
        if (_power.enabled()) {
            spdlog::info("NFC backend: disabling Cap power controls");
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

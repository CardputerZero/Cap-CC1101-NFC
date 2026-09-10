/*
 * Portions derived from M5Stack M5Unit-NFC.
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#include "nfc/st25r3916_driver.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

namespace cap_nfc::nfc {
namespace {

using namespace std::chrono_literals;

constexpr uint8_t kRegIoConfiguration1        = 0x00;
constexpr uint8_t kRegIoConfiguration2        = 0x01;
constexpr uint8_t kRegOperationControl        = 0x02;
constexpr uint8_t kRegModeDefinition          = 0x03;
constexpr uint8_t kRegBitrateDefinition       = 0x04;
constexpr uint8_t kRegIso14443ASettings       = 0x05;
constexpr uint8_t kRegFelicaSettings          = 0x07;
constexpr uint8_t kRegNfcipPassiveTarget      = 0x08;
constexpr uint8_t kRegAuxiliaryDefinition     = 0x0A;
constexpr uint8_t kRegReceiverConfiguration1  = 0x0B;
constexpr uint8_t kRegReceiverConfiguration2  = 0x0C;
constexpr uint8_t kRegReceiverConfiguration3  = 0x0D;
constexpr uint8_t kRegReceiverConfiguration4  = 0x0E;
constexpr uint8_t kRegNoResponseTimer1        = 0x10;
constexpr uint8_t kRegTimerAndEmvControl      = 0x12;
constexpr uint8_t kRegNfcFieldOnGuardTimer    = 0x15;
constexpr uint8_t kRegMaskMainInterrupt       = 0x16;
constexpr uint8_t kRegMainInterrupt           = 0x1A;
constexpr uint8_t kRegErrorAndWakeupInterrupt = 0x1C;
constexpr uint8_t kRegPassiveTargetInterrupt  = 0x1D;
constexpr uint8_t kRegFifoStatus1             = 0x1E;
constexpr uint8_t kRegCollisionDisplay        = 0x20;
constexpr uint8_t kRegTransmitBytes1          = 0x22;
constexpr uint8_t kRegAdConverterOutput       = 0x25;
constexpr uint8_t kRegAntennaTuningControl1   = 0x26;
constexpr uint8_t kRegAntennaTuningControl2   = 0x27;
constexpr uint8_t kRegTxDriver                = 0x28;
constexpr uint8_t kRegPassiveTargetModulation = 0x29;
constexpr uint8_t kRegExternalFieldOn         = 0x2A;
constexpr uint8_t kRegExternalFieldOff        = 0x2B;
constexpr uint8_t kRegAuxiliaryDisplay        = 0x31;
constexpr uint8_t kRegIcIdentity              = 0x3F;

constexpr uint8_t kRegBEmdSuppression           = 0x05;
constexpr uint8_t kRegBCorrelatorConfiguration1 = 0x0C;
constexpr uint8_t kRegBCorrelatorConfiguration2 = 0x0D;
constexpr uint8_t kRegBResistiveAmModulation    = 0x2A;
constexpr uint8_t kRegBRegulatorDisplay         = 0x2C;
constexpr uint8_t kRegBOvershootConfiguration1  = 0x30;
constexpr uint8_t kRegBOvershootConfiguration2  = 0x31;
constexpr uint8_t kRegBUndershootConfiguration1 = 0x32;
constexpr uint8_t kRegBUndershootConfiguration2 = 0x33;

constexpr uint8_t kCommandSetDefault         = 0xC1;
constexpr uint8_t kCommandStopAll            = 0xC2;
constexpr uint8_t kCommandTransmitWithCrc    = 0xC4;
constexpr uint8_t kCommandTransmitWithoutCrc = 0xC5;
constexpr uint8_t kCommandTransmitWupa       = 0xC7;
constexpr uint8_t kCommandInitialFieldOn     = 0xC8;
constexpr uint8_t kCommandResetRxGain        = 0xD5;
constexpr uint8_t kCommandAdjustRegulators   = 0xD6;
constexpr uint8_t kCommandClearFifo          = 0xDB;
constexpr uint8_t kCommandMeasurePowerSupply = 0xDF;
constexpr uint8_t kCommandSpaceBAccess       = 0xFB;
constexpr uint8_t kCommandTestAccess         = 0xFC;

constexpr uint8_t kOperationOscillator                      = 0x80;
constexpr uint8_t kOperationReceiver                        = 0x40;
constexpr uint8_t kOperationTransmitter                     = 0x08;
constexpr uint8_t kOperationWakeup                          = 0x04;
constexpr uint8_t kOperationFieldDetector                   = 0x03;
constexpr uint8_t kOperationFieldDetectorCollisionAvoidance = 0x01;

constexpr uint8_t kAuxNoCrcReceive         = 0x80;
constexpr uint8_t kAuxDisableCorrelator    = 0x04;
constexpr uint8_t kAuxDisplayTransmitterOn = 0x20;
constexpr uint8_t kAuxDisplayOscillatorOk  = 0x10;
constexpr uint8_t kTimerNrtEmv             = 0x02;

constexpr uint32_t kInterruptOscillator     = UINT32_C(0x80000000);
constexpr uint32_t kInterruptRxStart        = UINT32_C(0x20000000);
constexpr uint32_t kInterruptRxEnd          = UINT32_C(0x10000000);
constexpr uint32_t kInterruptTxEnd          = UINT32_C(0x08000000);
constexpr uint32_t kInterruptCollision      = UINT32_C(0x04000000);
constexpr uint32_t kInterruptNoResponse     = UINT32_C(0x00400000);
constexpr uint32_t kInterruptFieldCollision = UINT32_C(0x00040000);
constexpr uint32_t kInterruptFieldGuardDone = UINT32_C(0x00020000);
constexpr uint32_t kInterruptCrcError       = UINT32_C(0x00008000);
constexpr uint32_t kInterruptParityError    = UINT32_C(0x00004000);
constexpr uint32_t kInterruptHardError      = UINT32_C(0x00003000);
constexpr uint32_t kInterruptRxErrors       = kInterruptCrcError | kInterruptParityError | kInterruptHardError;

constexpr uint8_t kReadRegisterOperation             = 0x40;
constexpr uint8_t kLoadFifoOperation                 = 0x80;
constexpr uint8_t kReadFifoOperation                 = 0x9F;
constexpr uint8_t kIdentityTypeMask                  = 0xF8;
constexpr uint8_t kSt25r3916IdentityType             = 0x28;
constexpr std::size_t kMaximumFifoSize               = 512;
constexpr std::size_t kMaximumNdefSize               = 2040;
constexpr std::size_t kNdefCacheRemovalConfirmations = 3;
// A four-slot SENSF_REQ can take roughly 20 ms on the wire.  Phones that
// expose FeliCa often need another scheduling interval before they answer, so
// keep the complete polling window aligned with the M5/RFAL 40 ms budget.
constexpr std::chrono::milliseconds kNfcFPollTimeout        = 40ms;
constexpr std::chrono::milliseconds kNfcFTransmitDrainGuard = 1ms;
constexpr std::chrono::milliseconds kNfcFFifoSettleWindow   = 3ms;
constexpr std::size_t kNfcFResponseMinimumSize              = 18;
constexpr std::size_t kNfcFResponseMaximumSize              = 20;
constexpr uint8_t kNfcFFourSlotTsn                          = 0x03;

// ST25R3916's NFC-F FIFO payload starts with the FeliCa command byte. The
// on-air length byte (0x06 for SENSF_REQ) is generated by the NFC-F framing
// logic and must not be inserted into this payload. Four time slots give
// phones and multi-card fields a chance to answer outside slot zero.
constexpr std::array<uint8_t, 5> kSensfRequest{0x00, 0xFF, 0xFF, 0x00, kNfcFFourSlotTsn};

uint16_t noResponseTimerValue(std::chrono::milliseconds timeout, bool slowStep)
{
    constexpr uint64_t kCarrierHz = 13'560'000;
    const uint64_t step           = slowStep ? 4096 : 64;
    const uint64_t numerator      = static_cast<uint64_t>(std::max<int64_t>(1, timeout.count())) * kCarrierHz;
    const uint64_t denominator    = step * 1000;
    return static_cast<uint16_t>(std::clamp<uint64_t>((numerator + denominator - 1) / denominator, 1, 0xFFFF));
}

uint8_t selectCommand(uint8_t level)
{
    return static_cast<uint8_t>(0x91 + level * 2);
}

std::string typeNameForSak(uint8_t sak)
{
    switch (sak) {
        case 0x00:
            return "NFC Forum Type 2 / Ultralight-compatible";
        case 0x08:
        case 0x28:
            return "MIFARE Classic 1K-compatible";
        case 0x09:
            return "MIFARE Classic Mini-compatible";
        case 0x18:
        case 0x38:
            return "MIFARE Classic 4K-compatible";
        default:
            return (sak & 0x20) != 0 ? "NFC-A / ISO-DEP (Type 4-compatible)" : "NFC-A tag";
    }
}

}  // namespace

St25r3916Driver::St25r3916Driver(St25r3916Transport& transport, St25r3916DriverConfig config)
    : _transport(transport), _config(config)
{
}

St25r3916ChipInfo St25r3916Driver::initialize(const CancellationToken& cancellation)
{
    stop();
    cancellation.throwIfCancellationRequested();
    _transport.sleepFor(50ms, cancellation);

    spdlog::info("ST25R3916 driver: probing IC identity");

    St25r3916ChipInfo chip;
    for (int attempt = 1; attempt <= 5; ++attempt) {
        cancellation.throwIfCancellationRequested();
        chip.identity = readRegister(kRegIcIdentity);
        chip.type     = static_cast<uint8_t>((chip.identity & kIdentityTypeMask) >> 3);
        chip.revision = static_cast<uint8_t>(chip.identity & 0x07);
        spdlog::debug("ST25R3916 driver: identity attempt {} returned 0x{:02X} (type=0x{:02X}, revision={})", attempt,
                      chip.identity, chip.type, chip.revision);
        if ((chip.identity & kIdentityTypeMask) == kSt25r3916IdentityType) {
            break;
        }
        _transport.sleepFor(20ms, cancellation);
    }
    if ((chip.identity & kIdentityTypeMask) != kSt25r3916IdentityType) {
        throw std::runtime_error("unexpected IC identity 0x" + bytesToHex({chip.identity}, "") +
                                 " (expected ST25R3916 type bits 0x28)");
    }

    spdlog::info("ST25R3916 driver: detected identity=0x{:02X}, type=0x{:02X}, revision={}", chip.identity, chip.type,
                 chip.revision);

    directCommand(kCommandStopAll);
    modifyRegister(kRegOperationControl, 0, kOperationTransmitter | kOperationReceiver);
    _transport.sleepFor(2ms, cancellation);

    // ST errata requires this test-access frame immediately after Set Default.
    spdlog::info("ST25R3916 driver: applying Set Default and overheat-protection errata");
    directCommand(kCommandSetDefault);
    const std::array<uint8_t, 2> protection{0x04, 0x10};
    directCommand(kCommandTestAccess, protection.data(), protection.size());

    // The schematic connects the analog VDD rails to +5 V and only VDD_IO to 3.3 V.
    // M5's Cap driver enables both MISO pull-downs. They also proved necessary for
    // stable direction switching on the CardputerZero's translated SPI bus.
    const uint8_t misoPullDowns =
        static_cast<uint8_t>((_config.misoPullWhenDeselected ? 0x08 : 0) | (_config.misoPullWhenSelected ? 0x10 : 0));
    const std::array<uint8_t, 2> ioConfiguration{0x07, static_cast<uint8_t>(0x24 | misoPullDowns)};
    writeRegisters(kRegIoConfiguration1, ioConfiguration.data(), ioConfiguration.size());
    spdlog::info("ST25R3916 driver: IO_CONF2=0x{:02X} (MISO pull when deselected={}, selected={})", ioConfiguration[1],
                 _config.misoPullWhenDeselected, _config.misoPullWhenSelected);
    writeRegister(kRegTxDriver, 0xD0);

    writeRegisterB(kRegBResistiveAmModulation, 0x80);
    writeRegisterB(kRegBResistiveAmModulation, 0x00);
    writeRegister(kRegExternalFieldOn, 0x13);
    writeRegister(kRegExternalFieldOff, 0x02);
    modifyRegister(kRegNfcipPassiveTarget, 0x50, 0xF0);
    writeRegister(kRegPassiveTargetModulation, 0x5F);
    writeRegisterB(kRegBEmdSuppression, 0x40);
    writeRegister(kRegAntennaTuningControl1, 0x82);
    writeRegister(kRegAntennaTuningControl2, 0x82);
    directCommand(kCommandClearFifo);

    writeRegister32(kRegMaskMainInterrupt, UINT32_MAX);
    clearInterrupts();
    enableOscillator(cancellation);
    writeRegister32(kRegMaskMainInterrupt, 0);

    directCommand(kCommandMeasurePowerSupply);
    _transport.sleepFor(1ms, cancellation);
    chip.measuredSupply = readRegister(kRegAdConverterOutput);
    spdlog::info("ST25R3916 driver: MEASURE_VDD raw ADC=0x{:02X}; IO_CONF2.sup3V=0 for schematic +5 V VDD",
                 chip.measuredSupply);

    directCommand(kCommandAdjustRegulators);
    _transport.sleepFor(5ms, cancellation);
    chip.regulatorDisplay = readRegisterB(kRegBRegulatorDisplay);
    spdlog::info("ST25R3916 driver: regulator display=0x{:02X}", chip.regulatorDisplay);

    configureNfcA(cancellation);
    _ready                    = true;
    _discovery_enabled        = true;
    _active_protocol          = ActiveProtocol::NfcA;
    _field_reset_required     = false;
    _failed_wakeups           = 0;
    _consecutive_no_tags      = 0;
    _consecutive_nfcf_no_tags = 0;
    return chip;
}

void St25r3916Driver::startNfcA(const CancellationToken& cancellation)
{
    if (!_ready) {
        throw std::runtime_error("ST25R3916 driver is not initialized");
    }
    if (_discovery_enabled && _active_protocol == ActiveProtocol::NfcA) {
        return;
    }
    configureNfcA(cancellation);
    _discovery_enabled        = true;
    _active_protocol          = ActiveProtocol::NfcA;
    _field_reset_required     = false;
    _failed_wakeups           = 0;
    _consecutive_no_tags      = 0;
    _consecutive_nfcf_no_tags = 0;
}

void St25r3916Driver::stopNfcA() noexcept
{
    if (!_ready || !_discovery_enabled) {
        return;
    }
    try {
        directCommand(kCommandStopAll);
        modifyRegister(kRegOperationControl, 0, kOperationTransmitter | kOperationReceiver);
    } catch (const std::exception& exception) {
        spdlog::warn("ST25R3916 driver: failed to disable the NFC field: {}", exception.what());
    }
    _discovery_enabled        = false;
    _active_protocol          = ActiveProtocol::NfcA;
    _field_reset_required     = false;
    _failed_wakeups           = 0;
    _consecutive_no_tags      = 0;
    _consecutive_nfcf_no_tags = 0;
    _cached_tag.reset();
}

void St25r3916Driver::startNfcF(const CancellationToken& cancellation)
{
    if (!_ready) {
        throw std::runtime_error("ST25R3916 driver is not initialized");
    }
    if (_discovery_enabled && _active_protocol == ActiveProtocol::NfcF) {
        return;
    }
    configureNfcF(cancellation);
    _discovery_enabled        = true;
    _active_protocol          = ActiveProtocol::NfcF;
    _field_reset_required     = false;
    _failed_wakeups           = 0;
    _consecutive_nfcf_no_tags = 0;
}

void St25r3916Driver::stopNfcF() noexcept
{
    stopNfcA();
}

void St25r3916Driver::stop() noexcept
{
    stopNfcA();
    _ready                    = false;
    _discovery_enabled        = false;
    _active_protocol          = ActiveProtocol::NfcA;
    _field_reset_required     = false;
    _storedInterrupts         = 0;
    _failed_wakeups           = 0;
    _consecutive_no_tags      = 0;
    _consecutive_nfcf_no_tags = 0;
    _cached_tag.reset();
    try {
        directCommand(kCommandStopAll);
        modifyRegister(kRegOperationControl, 0, kOperationTransmitter | kOperationReceiver);
        writeRegister32(kRegMaskMainInterrupt, UINT32_MAX);
    } catch (const std::exception& exception) {
        spdlog::debug("ST25R3916 driver: stop skipped: {}", exception.what());
    }
}

St25r3916NfcAPollResult St25r3916Driver::pollNfcA(const CancellationToken& cancellation)
{
    if (!_ready || !_discovery_enabled) {
        throw std::runtime_error("ST25R3916 NFC-A discovery is not active");
    }
    cancellation.throwIfCancellationRequested();
    if (_active_protocol != ActiveProtocol::NfcA) {
        spdlog::debug("ST25R3916 driver: switching discovery protocol NFC-F -> NFC-A");
        configureNfcA(cancellation);
        _active_protocol          = ActiveProtocol::NfcA;
        _field_reset_required     = false;
        _failed_wakeups           = 0;
        _consecutive_no_tags      = 0;
        _consecutive_nfcf_no_tags = 0;
    }
    if (_field_reset_required) {
        spdlog::debug("ST25R3916 driver: resetting the NFC-A field after an incomplete protocol exchange");
        resetNfcAField(cancellation);
        _field_reset_required = false;
    }

    St25r3916NfcATag tag;
    switch (requestWakeup(tag.atqa, cancellation)) {
        case WakeupResult::NoTag:
            ++_consecutive_no_tags;
            if (_consecutive_no_tags >= kNdefCacheRemovalConfirmations) {
                _cached_tag.reset();
            }
            return {St25r3916NfcAPollKind::NoTag, std::nullopt};
        case WakeupResult::Inconclusive:
            _consecutive_no_tags  = 0;
            _field_reset_required = true;
            return {St25r3916NfcAPollKind::Inconclusive, std::nullopt};
        case WakeupResult::Response:
            _consecutive_no_tags = 0;
            break;
    }

    const auto protocolFailure = [this]() {
        _field_reset_required = true;
        return St25r3916NfcAPollResult{St25r3916NfcAPollKind::Inconclusive, std::nullopt};
    };

    for (uint8_t level = 1; level <= 3; ++level) {
        std::array<uint8_t, 5> anticollision{};
        if (!anticollisionLevel(level, anticollision, cancellation)) {
            spdlog::warn("ST25R3916 driver: NFC-A anticollision failed at cascade level {}", level);
            return protocolFailure();
        }

        uint8_t bcc = 0;
        for (uint8_t value : anticollision) {
            bcc ^= value;
        }
        if (bcc != 0) {
            spdlog::warn("ST25R3916 driver: invalid NFC-A BCC at cascade level {}", level);
            return protocolFailure();
        }

        const bool cascadeTag = anticollision[0] == 0x88;
        const auto begin      = anticollision.begin() + (cascadeTag ? 1 : 0);
        const auto end        = anticollision.begin() + 4;
        tag.uid.insert(tag.uid.end(), begin, end);

        if (!selectLevel(level, anticollision, tag.sak, cancellation)) {
            spdlog::warn("ST25R3916 driver: NFC-A select failed at cascade level {}", level);
            return protocolFailure();
        }

        const bool moreLevels = (tag.sak & 0x04) != 0;
        if (cascadeTag != moreLevels) {
            spdlog::warn("ST25R3916 driver: inconsistent NFC-A cascade tag/SAK at level {}", level);
            return protocolFailure();
        }
        if (!moreLevels) {
            tag.typeName = typeNameForSak(tag.sak);
            if (_cached_tag && _cached_tag->uid == tag.uid) {
                tag.ndefSupported = _cached_tag->ndefSupported;
                tag.ndefReadable  = _cached_tag->ndefReadable;
                tag.ndefCapacity  = _cached_tag->ndefCapacity;
                tag.ndefMessage   = _cached_tag->ndefMessage;
            } else if (tag.sak == 0x00 && !readType2Ndef(tag, cancellation)) {
                spdlog::debug("ST25R3916 driver: Type 2 tag selected, but no readable NDEF data was found");
            }
            if (!haltSelectedTag(cancellation)) {
                spdlog::warn("ST25R3916 driver: HLTA frame did not finish transmitting");
                _field_reset_required = true;
            }
            _failed_wakeups = 0;
            _cached_tag     = tag;
            return {St25r3916NfcAPollKind::Tag, std::move(tag)};
        }
        if (level == 3) {
            spdlog::warn("ST25R3916 driver: inconsistent NFC-A cascade response at level {}", level);
            return protocolFailure();
        }
    }
    return protocolFailure();
}

St25r3916NfcFPollResult St25r3916Driver::pollNfcF(const CancellationToken& cancellation)
{
    if (!_ready || !_discovery_enabled) {
        throw std::runtime_error("ST25R3916 NFC-F discovery is not active");
    }
    cancellation.throwIfCancellationRequested();

    if (_active_protocol != ActiveProtocol::NfcF) {
        spdlog::debug("ST25R3916 driver: switching discovery protocol NFC-A -> NFC-F");
        configureNfcF(cancellation);
        _active_protocol          = ActiveProtocol::NfcF;
        _field_reset_required     = false;
        _failed_wakeups           = 0;
        _consecutive_nfcf_no_tags = 0;
    }
    if (_field_reset_required) {
        spdlog::debug("ST25R3916 driver: resetting the NFC-F field after an incomplete protocol exchange");
        resetNfcFField(cancellation);
        _field_reset_required = false;
    }

    // SENSF_REQ is five bytes in the ST25R3916 FIFO: command 0x00, wildcard
    // system code, no request data, and a four-slot TSN. The length byte
    // (0x06) is part of the NFC-F wire framing and is not written to the FIFO.
    ensureNfcFField(cancellation);
    prepareTransceive();
    setNoResponseTimeout(kNfcFPollTimeout);
    modifyRegister(kRegAuxiliaryDefinition, 0, kAuxNoCrcReceive);
    clearInterrupts();
    directCommand(kCommandClearFifo);
    writeFifo(kSensfRequest.data(), kSensfRequest.size());
    setTransmitLength(kSensfRequest.size());
    directCommand(kCommandTransmitWithCrc);

    // TXE, RXS and RXE are separate events on the ST25R3916. In particular,
    // the silicon can signal RXS without a later RXE for a valid frame. Keep a
    // single deadline for the whole exchange and use the FIFO water mark as a
    // bounded fallback, matching the workaround used by RFAL/M5's driver.
    constexpr uint32_t kNfcFInterrupts = kInterruptTxEnd | kInterruptRxStart | kInterruptRxEnd | kInterruptCollision |
                                         kInterruptNoResponse | kInterruptRxErrors;
    constexpr uint32_t kNfcFReceiveActivity =
        kInterruptRxStart | kInterruptRxEnd | kInterruptCollision | kInterruptRxErrors;
    const auto exchangeStart   = std::chrono::steady_clock::now();
    const auto txDrainDeadline = exchangeStart + kNfcFTransmitDrainGuard;
    const auto deadline        = exchangeStart + kNfcFPollTimeout + 2ms;
    uint32_t flags             = 0;
    bool frameReady            = false;
    bool receiveWindow         = false;
    bool transmitComplete      = false;
    std::size_t fifoAvailable  = 0;
    const auto inspectFifo     = [&]() {
        fifoAvailable = fifoSize();
        // Once the request has had time to drain, bytes beyond the five-byte
        // request are evidence of a response even if the RX edge was missed.
        if (fifoAvailable > kSensfRequest.size()) {
            receiveWindow = true;
        }
        if (fifoAvailable >= kNfcFResponseMinimumSize) {
            frameReady = true;
            return true;
        }
        return false;
    };
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        // The FIFO is shared by TX and RX. Do not inspect it before the TX
        // bytes have drained, otherwise the request itself can look like a
        // partial response. The time guard also covers a lost TXE/RXS edge.
        if ((transmitComplete || now >= txDrainDeadline) && inspectFifo()) {
            break;
        }

        if (now >= deadline) {
            break;
        }
        const auto remaining    = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const uint32_t observed = waitForInterrupt(kNfcFInterrupts, std::max(remaining, 1ms), cancellation);
        flags |= observed;
        transmitComplete = transmitComplete || (observed & kInterruptTxEnd) != 0;
        receiveWindow    = receiveWindow || (observed & kNfcFReceiveActivity) != 0;

        if ((observed & kInterruptRxErrors) != 0 || (observed & kInterruptCollision) != 0) {
            break;
        }
        // NRE can be latched before a delayed RXS edge on translated buses.
        // Keep the short grace period alive instead of immediately switching
        // protocols; the FIFO and later RX edge decide whether a frame exists.
        if ((transmitComplete || std::chrono::steady_clock::now() >= txDrainDeadline) && inspectFifo()) {
            break;
        }
        if ((flags & kInterruptRxEnd) != 0) {
            // RXE means no more bytes are expected. A complete FIFO frame was
            // checked above; an incomplete frame is retained for diagnostics.
            break;
        }
        // RX_START and NRE both keep the loop alive until the bounded
        // deadline. This handles a late response and a missing RXE edge.
    }

    const uint32_t allFlags = flags | _storedInterrupts;

    // Always sample the FIFO once more after the exchange loop.  RXE can be
    // observed just before the final bytes become visible over SPI, and the
    // old "already checked" shortcut could therefore turn a valid response
    // into an inconclusive result.  Keep the largest count seen: the FIFO is
    // shared by TX and RX, so the count can briefly fall while TX drains.
    const std::size_t previousFifo = fifoAvailable;
    fifoAvailable                  = std::max(fifoAvailable, fifoSize());
    if (fifoAvailable != previousFifo) {
        spdlog::debug("ST25R3916 driver: NFC-F final FIFO sample grew from {} to {} bytes", previousFifo,
                      fifoAvailable);
    }

    // Give a response that has already asserted RX activity a short, bounded
    // settling window.  This covers translated IRQ/FIFO timing without
    // adding latency to a clean no-tag poll.
    const bool receiveEdge = receiveWindow || (allFlags & (kInterruptRxStart | kInterruptRxEnd)) != 0;
    if (!frameReady && (receiveEdge || fifoAvailable > kSensfRequest.size()) &&
        (allFlags & (kInterruptRxErrors | kInterruptCollision)) == 0) {
        const auto settleDeadline = std::chrono::steady_clock::now() + kNfcFFifoSettleWindow;
        while (!frameReady && std::chrono::steady_clock::now() < settleDeadline) {
            _transport.sleepFor(1ms, cancellation);
            const std::size_t settledFifo = fifoSize();
            if (settledFifo > fifoAvailable) {
                spdlog::debug("ST25R3916 driver: NFC-F settled FIFO sample grew from {} to {} bytes", fifoAvailable,
                              settledFifo);
                fifoAvailable = settledFifo;
            }
            frameReady = fifoAvailable >= kNfcFResponseMinimumSize;
        }
    }

    const bool fifoResponseEvidence = fifoAvailable > kSensfRequest.size();
    const bool receiveActivity      = receiveWindow || fifoResponseEvidence || (allFlags & kNfcFReceiveActivity) != 0;
    if ((allFlags & kInterruptRxErrors) != 0 || (allFlags & kInterruptCollision) != 0) {
        _field_reset_required = true;
        spdlog::debug("ST25R3916 driver: NFC-F SENSF_REQ receive error/collision (flags=0x{:08X})", allFlags);
        return {St25r3916NfcFPollKind::Inconclusive, std::nullopt};
    }
    if (!frameReady) {
        const bool inferredNoResponse = !receiveActivity && fifoAvailable <= kSensfRequest.size() &&
                                        (transmitComplete || std::chrono::steady_clock::now() >= deadline);
        if ((allFlags & kInterruptNoResponse) != 0 && !receiveActivity) {
            ++_consecutive_nfcf_no_tags;
            return {St25r3916NfcFPollKind::NoTag, std::nullopt};
        }
        if (inferredNoResponse) {
            ++_consecutive_nfcf_no_tags;
            spdlog::debug(
                "ST25R3916 driver: NFC-F SENSF_REQ completed without RX activity; treating as no tag "
                "(flags=0x{:08X}, fifo={})",
                allFlags, fifoAvailable);
            return {St25r3916NfcFPollKind::NoTag, std::nullopt};
        }
        _field_reset_required = true;
        spdlog::debug("ST25R3916 driver: NFC-F SENSF_RES incomplete (flags=0x{:08X}, fifo={})", allFlags,
                      fifoAvailable);
        return {St25r3916NfcFPollKind::Inconclusive, std::nullopt};
    }

    std::array<uint8_t, kMaximumFifoSize> response{};
    for (int attempt = 0; attempt < 3 && fifoSize() < kNfcFResponseMinimumSize; ++attempt) {
        _transport.sleepFor(1ms, cancellation);
    }
    std::size_t actual = 0;
    if (!readFifo(response.data(), response.size(), actual) || actual < kNfcFResponseMinimumSize) {
        _field_reset_required = true;
        spdlog::debug("ST25R3916 driver: NFC-F SENSF_RES too short (fifo={})", actual);
        return {St25r3916NfcFPollKind::Inconclusive, std::nullopt};
    }
    if (response[0] < kNfcFResponseMinimumSize || response[0] > kNfcFResponseMaximumSize || response[0] > actual ||
        response[1] != 0x01) {
        _field_reset_required = true;
        spdlog::debug("ST25R3916 driver: invalid NFC-F SENSF_RES (length=0x{:02X}, fifo={}, command=0x{:02X})",
                      response[0], actual, response[1]);
        return {St25r3916NfcFPollKind::Inconclusive, std::nullopt};
    }

    St25r3916NfcFTag tag;
    std::copy_n(response.begin() + 2, tag.idm.size(), tag.idm.begin());
    std::copy_n(response.begin() + 10, tag.pmm.size(), tag.pmm.begin());
    tag.typeName              = "NFC-F / FeliCa (NFC Forum Type 3)";
    _failed_wakeups           = 0;
    _consecutive_nfcf_no_tags = 0;
    spdlog::debug("ST25R3916 driver: NFC-F tag detected (IDm={})",
                  bytesToHex(std::vector<uint8_t>(tag.idm.begin(), tag.idm.end())));
    return {St25r3916NfcFPollKind::Tag, std::move(tag)};
}

bool St25r3916Driver::ready() const noexcept
{
    return _ready;
}

bool St25r3916Driver::discoveryEnabled() const noexcept
{
    return _discovery_enabled;
}

std::vector<uint8_t> St25r3916Driver::exchange(const std::vector<uint8_t>& transmit)
{
    if (transmit.empty()) {
        throw std::invalid_argument("ST25R3916 SPI transaction is empty");
    }
    std::vector<uint8_t> receive(transmit.size());
    _transport.transfer(transmit.data(), receive.data(), transmit.size());
    return receive;
}

uint8_t St25r3916Driver::readRegister(uint8_t reg)
{
    uint8_t value = 0;
    readRegisters(reg, &value, 1);
    return value;
}

void St25r3916Driver::readRegisters(uint8_t reg, uint8_t* values, std::size_t size)
{
    if (!values || size == 0 || size > 255) {
        throw std::invalid_argument("invalid ST25R3916 register read size");
    }
    std::vector<uint8_t> transmit(size + 1, 0);
    transmit[0]        = static_cast<uint8_t>(kReadRegisterOperation | (reg & 0x3F));
    const auto receive = exchange(transmit);
    std::copy_n(receive.begin() + 1, size, values);
}

void St25r3916Driver::writeRegister(uint8_t reg, uint8_t value)
{
    writeRegisters(reg, &value, 1);
}

void St25r3916Driver::writeRegisters(uint8_t reg, const uint8_t* values, std::size_t size)
{
    if (!values || size == 0 || size > 255) {
        throw std::invalid_argument("invalid ST25R3916 register write size");
    }
    std::vector<uint8_t> transmit(size + 1);
    transmit[0] = static_cast<uint8_t>(reg & 0x3F);
    std::copy_n(values, size, transmit.begin() + 1);
    (void)exchange(transmit);
}

void St25r3916Driver::writeRegister16(uint8_t reg, uint16_t value)
{
    const std::array<uint8_t, 2> bytes{static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    writeRegisters(reg, bytes.data(), bytes.size());
}

void St25r3916Driver::writeRegister32(uint8_t reg, uint32_t value)
{
    const std::array<uint8_t, 4> bytes{static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
                                       static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    writeRegisters(reg, bytes.data(), bytes.size());
}

uint16_t St25r3916Driver::readRegister16(uint8_t reg)
{
    std::array<uint8_t, 2> bytes{};
    readRegisters(reg, bytes.data(), bytes.size());
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
}

uint8_t St25r3916Driver::readRegisterB(uint8_t reg)
{
    const auto receive =
        exchange({kCommandSpaceBAccess, static_cast<uint8_t>(kReadRegisterOperation | (reg & 0x3F)), 0});
    return receive[2];
}

void St25r3916Driver::writeRegisterB(uint8_t reg, uint8_t value)
{
    (void)exchange({kCommandSpaceBAccess, static_cast<uint8_t>(reg & 0x3F), value});
}

void St25r3916Driver::directCommand(uint8_t command)
{
    (void)exchange({command});
}

void St25r3916Driver::directCommand(uint8_t command, const uint8_t* data, std::size_t size)
{
    if ((!data && size != 0) || size > 255) {
        throw std::invalid_argument("invalid ST25R3916 direct command payload");
    }
    std::vector<uint8_t> transmit(size + 1);
    transmit[0] = command;
    if (size != 0) {
        std::copy_n(data, size, transmit.begin() + 1);
    }
    (void)exchange(transmit);
}

void St25r3916Driver::modifyRegister(uint8_t reg, uint8_t setMask, uint8_t clearMask)
{
    const uint8_t current = readRegister(reg);
    const uint8_t updated = static_cast<uint8_t>((current & ~clearMask) | setMask);
    if (updated != current) {
        writeRegister(reg, updated);
    }
}

void St25r3916Driver::clearInterrupts()
{
    _storedInterrupts = 0;
    for (int pass = 0; pass < 4; ++pass) {
        (void)readInterrupts();
        if (!_transport.interruptAsserted()) {
            break;
        }
    }
}

uint32_t St25r3916Driver::readInterrupts()
{
    const uint8_t error         = readRegister(kRegErrorAndWakeupInterrupt);
    const uint16_t mainAndTimer = readRegister16(kRegMainInterrupt);
    const uint8_t passive       = readRegister(kRegPassiveTargetInterrupt);
    return (static_cast<uint32_t>(mainAndTimer) << 16) | (static_cast<uint32_t>(error) << 8) | passive;
}

uint32_t St25r3916Driver::waitForInterrupt(uint32_t flags, std::chrono::milliseconds timeout,
                                           const CancellationToken& cancellation)
{
    const auto deadline = std::chrono::steady_clock::now() + std::max(timeout, 0ms);
    while (true) {
        cancellation.throwIfCancellationRequested();

        if (_transport.interruptAsserted()) {
            for (int pass = 0; pass < 4; ++pass) {
                _storedInterrupts |= readInterrupts();
                if (!_transport.interruptAsserted()) {
                    break;
                }
            }
        }

        const uint32_t matched = _storedInterrupts & flags;
        if (matched != 0) {
            _storedInterrupts &= ~matched;
            return matched;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return 0;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const auto slice     = std::min(remaining, 2ms);
        if (_transport.waitForInterrupt(std::max(slice, 1ms), cancellation)) {
            _storedInterrupts |= readInterrupts();
        } else {
            // Polling is a deliberate fallback for a missed edge or a BSP without IRQ support.
            _storedInterrupts |= readInterrupts();
        }
    }
}

void St25r3916Driver::configureNfcA(const CancellationToken& cancellation)
{
    cancellation.throwIfCancellationRequested();
    directCommand(kCommandStopAll);
    modifyRegister(kRegOperationControl, 0, kOperationWakeup);

    writeRegister(kRegModeDefinition, 0x08);
    writeRegister(kRegBitrateDefinition, 0x00);
    writeRegister(kRegIso14443ASettings, 0x00);
    modifyRegister(kRegTimerAndEmvControl, 0, kTimerNrtEmv);
    modifyRegister(kRegAuxiliaryDefinition, 0, kAuxDisableCorrelator);
    writeRegisterB(kRegBOvershootConfiguration1, 0x40);
    writeRegisterB(kRegBOvershootConfiguration2, 0x03);
    writeRegisterB(kRegBUndershootConfiguration1, 0x40);
    writeRegisterB(kRegBUndershootConfiguration2, 0x03);
    writeRegisterB(kRegBCorrelatorConfiguration1, 0x47);
    writeRegisterB(kRegBCorrelatorConfiguration2, 0x00);
    writeRegister(kRegReceiverConfiguration1, 0x08);
    writeRegister(kRegReceiverConfiguration2, 0x2D);
    writeRegister(kRegReceiverConfiguration3, 0xD8);
    writeRegister(kRegReceiverConfiguration4, 0x22);
    directCommand(kCommandResetRxGain);
    writeRegister32(kRegMaskMainInterrupt, 0);
    clearInterrupts();
    enableField(cancellation);

    const uint8_t operation = readRegister(kRegOperationControl);
    const uint8_t auxiliary = readRegister(kRegAuxiliaryDisplay);
    spdlog::info("ST25R3916 driver: NFC-A 106 kbps field enabled (OP_CONTROL=0x{:02X}, AUX_DISPLAY=0x{:02X})",
                 operation, auxiliary);
}

void St25r3916Driver::configureNfcF(const CancellationToken& cancellation)
{
    cancellation.throwIfCancellationRequested();
    directCommand(kCommandStopAll);
    modifyRegister(kRegOperationControl, 0, kOperationWakeup | kOperationFieldDetector);

    // These values match M5Unit-NFC's UnitST25R3916::configure_nfc_f():
    // initiator FeliCa mode with transparent mode, 212 kbit/s in both
    // directions, and the receiver/correlator settings recommended by M5.
    writeRegister(kRegModeDefinition, 0x1C);
    writeRegister(kRegBitrateDefinition, 0x11);
    writeRegister(kRegFelicaSettings, 0x00);
    writeRegister(kRegAuxiliaryDefinition, 0x00);
    // Keep NRT running after RX starts so the remaining FeliCa time slots
    // stay inside one bounded polling window, matching RFAL's EMV handling.
    modifyRegister(kRegTimerAndEmvControl, kTimerNrtEmv, 0);
    writeRegisterB(kRegBCorrelatorConfiguration1, 0x54);
    writeRegisterB(kRegBCorrelatorConfiguration2, 0x00);
    writeRegister(kRegReceiverConfiguration1, 0x13);
    writeRegister(kRegReceiverConfiguration2, 0x3D);
    writeRegister(kRegReceiverConfiguration3, 0x00);
    writeRegister(kRegReceiverConfiguration4, 0x00);
    directCommand(kCommandResetRxGain);
    writeRegister32(kRegMaskMainInterrupt, 0);
    clearInterrupts();
    enableField(cancellation);

    const uint8_t operation = readRegister(kRegOperationControl);
    const uint8_t auxiliary = readRegister(kRegAuxiliaryDisplay);
    spdlog::info("ST25R3916 driver: NFC-F 212 kbps field enabled (OP_CONTROL=0x{:02X}, AUX_DISPLAY=0x{:02X})",
                 operation, auxiliary);
}

void St25r3916Driver::enableOscillator(const CancellationToken& cancellation)
{
    modifyRegister(kRegMaskMainInterrupt, 0, 0x80);
    clearInterrupts();
    modifyRegister(kRegOperationControl, kOperationOscillator, 0);
    const uint32_t interrupt = waitForInterrupt(kInterruptOscillator, 50ms, cancellation);
    if ((interrupt & kInterruptOscillator) == 0) {
        spdlog::debug("ST25R3916 driver: oscillator IRQ not observed; checking AUX_DISPLAY.osc_ok");
    }

    for (int attempt = 0; attempt < 10; ++attempt) {
        cancellation.throwIfCancellationRequested();
        if ((readRegister(kRegAuxiliaryDisplay) & kAuxDisplayOscillatorOk) != 0) {
            return;
        }
        _transport.sleepFor(1ms, cancellation);
    }
    throw std::runtime_error("oscillator did not report stable within 60 ms");
}

void St25r3916Driver::enableField(const CancellationToken& cancellation)
{
    directCommand(kCommandStopAll);
    modifyRegister(kRegOperationControl, 0,
                   kOperationTransmitter | kOperationReceiver | kOperationWakeup | kOperationFieldDetector);
    modifyRegister(kRegOperationControl, kOperationFieldDetectorCollisionAvoidance, kOperationFieldDetector);
    writeRegisterB(kRegNfcFieldOnGuardTimer, 33);  // 75 us + 33 * 151 us = 5.06 ms.
    clearInterrupts();
    directCommand(kCommandInitialFieldOn);

    const uint32_t flags = waitForInterrupt(kInterruptFieldGuardDone | kInterruptFieldCollision, 15ms, cancellation);
    if ((flags & kInterruptFieldCollision) != 0) {
        throw std::runtime_error("external RF field detected during collision avoidance");
    }
    if ((flags & kInterruptFieldGuardDone) == 0) {
        throw std::runtime_error("NFC field-on guard timer did not complete");
    }

    modifyRegister(kRegOperationControl, kOperationReceiver, kOperationWakeup);
    const uint8_t operation = readRegister(kRegOperationControl);
    const uint8_t auxiliary = readRegister(kRegAuxiliaryDisplay);
    if ((operation & kOperationTransmitter) == 0) {
        spdlog::error(
            "ST25R3916 driver: NFC field-on verification failed (flags=0x{:08X}, OP_CONTROL=0x{:02X}, "
            "AUX_DISPLAY=0x{:02X})",
            flags, operation, auxiliary);
        throw std::runtime_error("RF transmitter enable was not retained after field-on guard time");
    }
    if ((auxiliary & kAuxDisplayTransmitterOn) == 0) {
        spdlog::debug(
            "ST25R3916 driver: I_cat completed with tx_en set while AUX_DISPLAY.tx_on is clear at idle "
            "(OP_CONTROL=0x{:02X}, AUX_DISPLAY=0x{:02X})",
            operation, auxiliary);
    }
}

void St25r3916Driver::resetNfcAField(const CancellationToken& cancellation)
{
    directCommand(kCommandStopAll);
    writeRegister(kRegOperationControl, kOperationOscillator);
    _transport.sleepFor(6ms, cancellation);
    configureNfcA(cancellation);
}

void St25r3916Driver::resetNfcFField(const CancellationToken& cancellation)
{
    directCommand(kCommandStopAll);
    writeRegister(kRegOperationControl, kOperationOscillator);
    _transport.sleepFor(6ms, cancellation);
    configureNfcF(cancellation);
}

void St25r3916Driver::ensureNfcAField(const CancellationToken& cancellation)
{
    constexpr uint8_t expectedMask =
        kOperationOscillator | kOperationReceiver | kOperationTransmitter | kOperationWakeup | kOperationFieldDetector;
    constexpr uint8_t expectedValue =
        kOperationOscillator | kOperationReceiver | kOperationTransmitter | kOperationFieldDetectorCollisionAvoidance;

    const auto operationReady = [](uint8_t operation) { return (operation & expectedMask) == expectedValue; };
    std::array<uint8_t, 2> modeAndBitrate{};
    std::array<uint8_t, 4> receiverConfiguration{};
    uint8_t timerAndEmv     = 0;
    const auto profileReady = [&]() {
        readRegisters(kRegModeDefinition, modeAndBitrate.data(), modeAndBitrate.size());
        readRegisters(kRegReceiverConfiguration1, receiverConfiguration.data(), receiverConfiguration.size());
        timerAndEmv = readRegister(kRegTimerAndEmvControl);
        constexpr std::array<uint8_t, 4> expectedReceiver{0x08, 0x2D, 0xD8, 0x22};
        return modeAndBitrate[0] == 0x08 && modeAndBitrate[1] == 0x00 && receiverConfiguration == expectedReceiver &&
               (timerAndEmv & kTimerNrtEmv) == 0;
    };
    const auto fieldReady = [&](uint8_t operation) { return operationReady(operation) && profileReady(); };

    std::array<uint8_t, 3> operationSamples{};
    operationSamples[0] = readRegister(kRegOperationControl);
    if (fieldReady(operationSamples[0])) {
        return;
    }

    for (std::size_t index = 1; index < operationSamples.size(); ++index) {
        _transport.sleepFor(1ms, cancellation);
        operationSamples[index] = readRegister(kRegOperationControl);
        if (fieldReady(operationSamples[index])) {
            spdlog::debug("ST25R3916 driver: ignored transient OP_CONTROL readback 0x{:02X}; retry returned 0x{:02X}",
                          operationSamples[0], operationSamples[index]);
            return;
        }
    }

    const uint8_t identity = readRegister(kRegIcIdentity);
    spdlog::warn(
        "ST25R3916 driver: NFC-A field state drifted (OP_CONTROL samples=0x{:02X}/0x{:02X}/0x{:02X}, "
        "MODE=0x{:02X}, BITRATE=0x{:02X}, RX=0x{:02X}/0x{:02X}/0x{:02X}/0x{:02X}, TIMER=0x{:02X}, IDENTITY=0x{:02X}); "
        "reconfiguring the field",
        operationSamples[0], operationSamples[1], operationSamples[2], modeAndBitrate[0], modeAndBitrate[1],
        receiverConfiguration[0], receiverConfiguration[1], receiverConfiguration[2], receiverConfiguration[3],
        timerAndEmv, identity);
    if ((operationSamples.back() & kOperationOscillator) == 0) {
        enableOscillator(cancellation);
    }
    configureNfcA(cancellation);
}

void St25r3916Driver::ensureNfcFField(const CancellationToken& cancellation)
{
    constexpr uint8_t expectedMask =
        kOperationOscillator | kOperationReceiver | kOperationTransmitter | kOperationWakeup | kOperationFieldDetector;
    constexpr uint8_t expectedValue =
        kOperationOscillator | kOperationReceiver | kOperationTransmitter | kOperationFieldDetectorCollisionAvoidance;

    const auto operationReady = [](uint8_t operation) { return (operation & expectedMask) == expectedValue; };
    std::array<uint8_t, 2> modeAndBitrate{};
    std::array<uint8_t, 4> receiverConfiguration{};
    uint8_t timerAndEmv     = 0;
    const auto profileReady = [&]() {
        readRegisters(kRegModeDefinition, modeAndBitrate.data(), modeAndBitrate.size());
        readRegisters(kRegReceiverConfiguration1, receiverConfiguration.data(), receiverConfiguration.size());
        timerAndEmv = readRegister(kRegTimerAndEmvControl);
        constexpr std::array<uint8_t, 4> expectedReceiver{0x13, 0x3D, 0x00, 0x00};
        return modeAndBitrate[0] == 0x1C && modeAndBitrate[1] == 0x11 && receiverConfiguration == expectedReceiver &&
               (timerAndEmv & kTimerNrtEmv) != 0;
    };
    const auto fieldReady = [&](uint8_t operation) { return operationReady(operation) && profileReady(); };

    std::array<uint8_t, 3> operationSamples{};
    operationSamples[0] = readRegister(kRegOperationControl);
    if (fieldReady(operationSamples[0])) {
        return;
    }

    for (std::size_t index = 1; index < operationSamples.size(); ++index) {
        _transport.sleepFor(1ms, cancellation);
        operationSamples[index] = readRegister(kRegOperationControl);
        if (fieldReady(operationSamples[index])) {
            spdlog::debug(
                "ST25R3916 driver: ignored transient NFC-F OP_CONTROL readback 0x{:02X}; retry returned "
                "0x{:02X}",
                operationSamples[0], operationSamples[index]);
            return;
        }
    }

    const uint8_t identity = readRegister(kRegIcIdentity);
    spdlog::warn(
        "ST25R3916 driver: NFC-F field state drifted (OP_CONTROL samples=0x{:02X}/0x{:02X}/0x{:02X}, "
        "MODE=0x{:02X}, BITRATE=0x{:02X}, RX=0x{:02X}/0x{:02X}/0x{:02X}/0x{:02X}, TIMER=0x{:02X}, IDENTITY=0x{:02X}); "
        "reconfiguring the field",
        operationSamples[0], operationSamples[1], operationSamples[2], modeAndBitrate[0], modeAndBitrate[1],
        receiverConfiguration[0], receiverConfiguration[1], receiverConfiguration[2], receiverConfiguration[3],
        timerAndEmv, identity);
    if ((operationSamples.back() & kOperationOscillator) == 0) {
        enableOscillator(cancellation);
    }
    configureNfcF(cancellation);
}

void St25r3916Driver::prepareTransceive()
{
    directCommand(kCommandStopAll);
    directCommand(kCommandResetRxGain);
}

void St25r3916Driver::setNoResponseTimeout(std::chrono::milliseconds timeout)
{
    const bool slowStep = (readRegister(kRegTimerAndEmvControl) & 0x01) != 0;
    writeRegister16(kRegNoResponseTimer1, noResponseTimerValue(timeout, slowStep));
}

void St25r3916Driver::logWakeupDiagnostics(uint32_t flags, const CancellationToken& cancellation)
{
    ++_failed_wakeups;
    if (_failed_wakeups != 1 && (_failed_wakeups % 100) != 0) {
        return;
    }

    cancellation.throwIfCancellationRequested();
    const std::size_t fifo       = fifoSize();
    const uint8_t operation      = readRegister(kRegOperationControl);
    const uint8_t auxiliary      = readRegister(kRegAuxiliaryDisplay);
    const bool interruptAsserted = _transport.interruptAsserted();
    spdlog::info(
        "ST25R3916 driver: no NFC-A WUPA response (poll={}, flags=0x{:08X}, stored=0x{:08X}, fifo={}, IRQ={}, "
        "OP_CONTROL=0x{:02X}, AUX_DISPLAY=0x{:02X})",
        _failed_wakeups, flags, _storedInterrupts, fifo, interruptAsserted, operation, auxiliary);
}

St25r3916Driver::WakeupResult St25r3916Driver::requestWakeup(std::array<uint8_t, 2>& atqa,
                                                             const CancellationToken& cancellation)
{
    ensureNfcAField(cancellation);
    prepareTransceive();
    setNoResponseTimeout(4ms);
    writeRegister(kRegIso14443ASettings, 0x00);
    modifyRegister(kRegAuxiliaryDefinition, kAuxNoCrcReceive, 0);
    clearInterrupts();
    directCommand(kCommandClearFifo);
    setTransmitLength(0);
    directCommand(kCommandTransmitWupa);

    const uint32_t flags = waitForInterrupt(
        kInterruptRxEnd | kInterruptCollision | kInterruptNoResponse | kInterruptRxErrors, 6ms, cancellation);
    const bool receiveActivity = ((flags | _storedInterrupts) & (kInterruptRxStart | kInterruptRxEnd |
                                                                 kInterruptCollision | kInterruptRxErrors)) != 0;
    if ((flags & kInterruptRxErrors) != 0 || (flags & (kInterruptRxEnd | kInterruptCollision)) == 0) {
        logWakeupDiagnostics(flags, cancellation);
        return (flags & kInterruptNoResponse) != 0 && !receiveActivity ? WakeupResult::NoTag
                                                                       : WakeupResult::Inconclusive;
    }

    for (int attempt = 0; attempt < 3 && fifoSize() < atqa.size(); ++attempt) {
        _transport.sleepFor(1ms, cancellation);
    }
    std::size_t actual  = 0;
    const bool complete = readFifo(atqa.data(), atqa.size(), actual) && actual == atqa.size();
    if (!complete) {
        logWakeupDiagnostics(flags, cancellation);
        return WakeupResult::Inconclusive;
    }
    return WakeupResult::Response;
}

bool St25r3916Driver::anticollisionLevel(uint8_t level, std::array<uint8_t, 5>& response,
                                         const CancellationToken& cancellation)
{
    if (level < 1 || level > 3) {
        return false;
    }
    setNoResponseTimeout(8ms);
    writeRegister(kRegIso14443ASettings, 0x01);
    modifyRegister(kRegAuxiliaryDefinition, 0, kAuxNoCrcReceive);

    std::array<uint8_t, 7> frame{selectCommand(level), 0x20};
    std::size_t sentBytes      = 2;
    uint8_t sentBits           = 0;
    std::size_t responseOffset = 0;
    uint8_t collisionByte      = 1;

    for (int attempt = 0; attempt < 32; ++attempt) {
        prepareTransceive();
        clearInterrupts();
        directCommand(kCommandClearFifo);
        writeFifo(frame.data(), sentBytes + (sentBits != 0 ? 1 : 0));
        setTransmitLength(sentBytes, sentBits);
        directCommand(kCommandTransmitWithoutCrc);

        const uint32_t flags = waitForInterrupt(
            kInterruptRxEnd | kInterruptCollision | kInterruptNoResponse | kInterruptRxErrors, 10ms, cancellation);
        const bool collision = (flags & kInterruptCollision) != 0;
        if (!collision && (flags & kInterruptRxEnd) == 0) {
            return false;
        }

        std::size_t actual = 0;
        if (responseOffset >= response.size() ||
            !readFifo(response.data() + responseOffset, response.size() - responseOffset, actual) || actual == 0) {
            return false;
        }
        if (!collision) {
            return true;
        }

        const uint8_t collisionDisplay = readRegister(kRegCollisionDisplay);
        const uint8_t collisionBytes   = static_cast<uint8_t>((collisionDisplay >> 4) & 0x0F);
        const uint8_t collisionBits    = static_cast<uint8_t>((collisionDisplay >> 1) & 0x07);
        if (responseOffset + actual > response.size() || collisionBytes < 2 || collisionBytes > 6) {
            return false;
        }

        collisionByte = response[responseOffset + actual - 1];
        collisionByte = static_cast<uint8_t>(collisionByte | (1U << collisionBits));
        sentBytes     = collisionBytes + (collisionBits == 7 ? 1 : 0);
        sentBits      = static_cast<uint8_t>((collisionBits + 1) & 0x07);
        if (sentBytes >= frame.size()) {
            return false;
        }
        frame[1] = static_cast<uint8_t>((sentBytes << 4) | sentBits);
        std::copy_n(response.begin() + responseOffset, actual, frame.begin() + 2 + responseOffset);
        frame[sentBytes] = collisionByte;
        responseOffset   = actual - 1;

        if (sentBits != 0) {
            response[responseOffset] = static_cast<uint8_t>((response[responseOffset] >> sentBits) << sentBits);
            response[responseOffset] = static_cast<uint8_t>(response[responseOffset] | collisionByte);
        }
    }
    return false;
}

bool St25r3916Driver::selectLevel(uint8_t level, const std::array<uint8_t, 5>& anticollision, uint8_t& sak,
                                  const CancellationToken& cancellation)
{
    std::array<uint8_t, 7> frame{selectCommand(level), 0x70};
    std::copy(anticollision.begin(), anticollision.end(), frame.begin() + 2);
    std::array<uint8_t, 3> response{};
    std::size_t responseSize = response.size();
    if (!transceive(frame.data(), frame.size(), response.data(), responseSize, 8ms, response.size(), cancellation) ||
        responseSize != response.size()) {
        return false;
    }
    sak = response[0];
    return true;
}

bool St25r3916Driver::haltSelectedTag(const CancellationToken& cancellation)
{
    constexpr std::array<uint8_t, 2> halt{0x50, 0x00};

    prepareTransceive();
    writeRegister(kRegIso14443ASettings, 0x00);
    modifyRegister(kRegAuxiliaryDefinition, 0, kAuxNoCrcReceive);
    clearInterrupts();
    directCommand(kCommandClearFifo);
    writeFifo(halt.data(), halt.size());
    setTransmitLength(halt.size());
    directCommand(kCommandTransmitWithCrc);

    const uint32_t flags = waitForInterrupt(kInterruptTxEnd, 3ms, cancellation);
    _transport.sleepFor(1ms, cancellation);
    return (flags & kInterruptTxEnd) != 0;
}

bool St25r3916Driver::transceive(const uint8_t* transmit, std::size_t transmitSize, uint8_t* receive,
                                 std::size_t& receiveSize, std::chrono::milliseconds timeout,
                                 std::size_t minimumReceive, const CancellationToken& cancellation)
{
    if (!transmit || transmitSize == 0 || !receive || receiveSize == 0) {
        return false;
    }
    prepareTransceive();
    setNoResponseTimeout(timeout);
    writeRegister(kRegIso14443ASettings, 0x00);
    modifyRegister(kRegAuxiliaryDefinition, 0, kAuxNoCrcReceive);
    clearInterrupts();
    directCommand(kCommandClearFifo);
    writeFifo(transmit, transmitSize);
    setTransmitLength(transmitSize);
    directCommand(kCommandTransmitWithCrc);

    const uint32_t flags =
        waitForInterrupt(kInterruptRxEnd | kInterruptNoResponse | kInterruptRxErrors, timeout + 2ms, cancellation);
    if ((flags & (kInterruptNoResponse | kInterruptRxErrors)) != 0 || (flags & kInterruptRxEnd) == 0) {
        receiveSize = 0;
        return false;
    }
    for (int attempt = 0; attempt < 3 && fifoSize() < minimumReceive; ++attempt) {
        _transport.sleepFor(1ms, cancellation);
    }
    std::size_t actual = 0;
    if (!readFifo(receive, receiveSize, actual) || actual < minimumReceive) {
        receiveSize = 0;
        return false;
    }
    receiveSize = actual;
    return true;
}

bool St25r3916Driver::readFifo(uint8_t* values, std::size_t capacity, std::size_t& actual)
{
    actual = 0;
    if (!values || capacity == 0) {
        return false;
    }
    const std::size_t available = fifoSize();
    if (available == 0) {
        return false;
    }
    const std::size_t readSize = std::min(available, capacity);
    std::vector<uint8_t> transmit(readSize + 1, 0);
    transmit[0]        = kReadFifoOperation;
    const auto receive = exchange(transmit);
    std::copy_n(receive.begin() + 1, readSize, values);
    actual = readSize;
    return true;
}

std::size_t St25r3916Driver::fifoSize()
{
    const uint16_t status = readRegister16(kRegFifoStatus1);
    return static_cast<std::size_t>((status >> 8) | ((status & 0x00C0) << 2));
}

void St25r3916Driver::writeFifo(const uint8_t* values, std::size_t size)
{
    if (!values || size == 0 || size > kMaximumFifoSize) {
        throw std::invalid_argument("invalid ST25R3916 FIFO write size");
    }
    std::vector<uint8_t> transmit(size + 1);
    transmit[0] = kLoadFifoOperation;
    std::copy_n(values, size, transmit.begin() + 1);
    (void)exchange(transmit);
}

void St25r3916Driver::setTransmitLength(std::size_t bytes, uint8_t trailingBits)
{
    if (bytes > 0x1FF || trailingBits > 7) {
        throw std::invalid_argument("invalid ST25R3916 transmit length");
    }
    writeRegister16(kRegTransmitBytes1, static_cast<uint16_t>((static_cast<uint16_t>(bytes) << 3) | trailingBits));
}

bool St25r3916Driver::readType2Ndef(St25r3916NfcATag& tag, const CancellationToken& cancellation)
{
    std::array<uint8_t, 16> manufacturer{};
    if (!readType2Block(0, manufacturer, cancellation)) {
        return false;
    }

    const uint8_t* capability  = manufacturer.data() + 12;
    const std::size_t capacity = static_cast<std::size_t>(capability[2]) * 8;
    if (capability[0] != 0xE1 || (capability[1] >> 4) < 1 || capacity == 0 || capacity > kMaximumNdefSize) {
        return false;
    }

    tag.ndefSupported = true;
    tag.ndefCapacity  = capacity;
    tag.ndefReadable  = (capability[3] >> 4) == 0;
    if (!tag.ndefReadable) {
        return true;
    }

    std::vector<uint8_t> memory;
    memory.reserve(capacity);
    auto ensureBytes = [&](std::size_t required) {
        if (required > capacity) {
            return false;
        }
        while (memory.size() < required) {
            cancellation.throwIfCancellationRequested();
            const std::size_t pageIndex = 4 + memory.size() / 4;
            if (pageIndex > 0xFF) {
                spdlog::warn(
                    "ST25R3916 driver: Type 2 NDEF data extends beyond the first sector; sector select is not "
                    "implemented");
                return false;
            }
            std::array<uint8_t, 16> block{};
            if (!readType2Block(static_cast<uint8_t>(pageIndex), block, cancellation)) {
                return false;
            }
            const std::size_t append = std::min(block.size(), capacity - memory.size());
            memory.insert(memory.end(), block.begin(), block.begin() + append);
        }
        return true;
    };

    if (!ensureBytes(std::min<std::size_t>(16, capacity))) {
        return false;
    }

    std::size_t offset = 0;
    while (offset < capacity) {
        if (!ensureBytes(offset + 1)) {
            return false;
        }
        const uint8_t type = memory[offset++];
        if (type == 0x00) {
            continue;
        }
        if (type == 0xFE) {
            return true;
        }
        if (!ensureBytes(offset + 1)) {
            return false;
        }

        std::size_t length = memory[offset++];
        if (length == 0xFF) {
            if (!ensureBytes(offset + 2)) {
                return false;
            }
            length = (static_cast<std::size_t>(memory[offset]) << 8) | memory[offset + 1];
            offset += 2;
        }
        if (length > capacity - offset || !ensureBytes(offset + length)) {
            return false;
        }
        if (type == 0x03) {
            tag.ndefMessage.assign(memory.begin() + offset, memory.begin() + offset + length);
            return true;
        }
        offset += length;
    }
    return true;
}

bool St25r3916Driver::readType2Block(uint8_t page, std::array<uint8_t, 16>& block,
                                     const CancellationToken& cancellation)
{
    const std::array<uint8_t, 2> command{0x30, page};
    std::size_t size = block.size();
    return transceive(command.data(), command.size(), block.data(), size, 12ms, block.size(), cancellation) &&
           size == block.size();
}

}  // namespace cap_nfc::nfc

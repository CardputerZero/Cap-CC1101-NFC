/*
 * Portions derived from M5Stack M5Unit-NFC.
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "nfc/nfc_types.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cap_nfc::nfc {

class St25r3916Transport {
public:
    virtual ~St25r3916Transport() = default;

    St25r3916Transport(const St25r3916Transport&)            = delete;
    St25r3916Transport& operator=(const St25r3916Transport&) = delete;

    virtual void transfer(const uint8_t* transmit, uint8_t* receive, std::size_t size)                      = 0;
    virtual bool interruptAsserted() const                                                                  = 0;
    virtual bool waitForInterrupt(std::chrono::milliseconds timeout, const CancellationToken& cancellation) = 0;
    virtual void sleepFor(std::chrono::milliseconds duration, const CancellationToken& cancellation)        = 0;

protected:
    St25r3916Transport() = default;
};

struct St25r3916ChipInfo {
    uint8_t identity         = 0;
    uint8_t type             = 0;
    uint8_t revision         = 0;
    uint8_t measuredSupply   = 0;
    uint8_t regulatorDisplay = 0;
};

struct St25r3916NfcATag {
    std::array<uint8_t, 2> atqa{};
    std::vector<uint8_t> uid;
    uint8_t sak = 0;
    std::string typeName;
    bool ndefSupported       = false;
    bool ndefReadable        = false;
    std::size_t ndefCapacity = 0;
    std::vector<uint8_t> ndefMessage;
};

enum class St25r3916NfcAPollKind {
    NoTag,
    Inconclusive,
    Tag,
};

struct St25r3916NfcAPollResult {
    St25r3916NfcAPollKind kind = St25r3916NfcAPollKind::Inconclusive;
    std::optional<St25r3916NfcATag> tag;
};

struct St25r3916NfcFTag {
    std::array<uint8_t, 8> idm{};
    std::array<uint8_t, 8> pmm{};
    std::string typeName;
};

enum class St25r3916NfcFPollKind {
    NoTag,
    Inconclusive,
    Tag,
};

struct St25r3916NfcFPollResult {
    St25r3916NfcFPollKind kind = St25r3916NfcFPollKind::Inconclusive;
    std::optional<St25r3916NfcFTag> tag;
};

struct St25r3916DriverConfig {
    bool misoPullWhenDeselected = true;
    bool misoPullWhenSelected   = true;
};

class St25r3916Driver {
public:
    explicit St25r3916Driver(St25r3916Transport& transport, St25r3916DriverConfig config = {});
    ~St25r3916Driver() = default;

    St25r3916Driver(const St25r3916Driver&)            = delete;
    St25r3916Driver& operator=(const St25r3916Driver&) = delete;

    St25r3916ChipInfo initialize(const CancellationToken& cancellation);
    void startNfcA(const CancellationToken& cancellation);
    void stopNfcA() noexcept;
    void startNfcF(const CancellationToken& cancellation);
    void stopNfcF() noexcept;
    void stop() noexcept;
    St25r3916NfcAPollResult pollNfcA(const CancellationToken& cancellation);
    St25r3916NfcFPollResult pollNfcF(const CancellationToken& cancellation);
    bool ready() const noexcept;
    bool discoveryEnabled() const noexcept;

private:
    enum class ActiveProtocol {
        NfcA,
        NfcF,
    };

    enum class WakeupResult {
        NoTag,
        Inconclusive,
        Response,
    };

    St25r3916Transport& _transport;
    St25r3916DriverConfig _config;
    bool _ready                           = false;
    bool _discovery_enabled               = false;
    ActiveProtocol _active_protocol       = ActiveProtocol::NfcA;
    bool _field_reset_required            = false;
    uint32_t _storedInterrupts            = 0;
    std::size_t _failed_wakeups           = 0;
    std::size_t _consecutive_no_tags      = 0;
    std::size_t _consecutive_nfcf_no_tags = 0;
    std::optional<St25r3916NfcATag> _cached_tag;

    std::vector<uint8_t> exchange(const std::vector<uint8_t>& transmit);
    uint8_t readRegister(uint8_t reg);
    void readRegisters(uint8_t reg, uint8_t* values, std::size_t size);
    void writeRegister(uint8_t reg, uint8_t value);
    void writeRegisters(uint8_t reg, const uint8_t* values, std::size_t size);
    void writeRegister16(uint8_t reg, uint16_t value);
    void writeRegister32(uint8_t reg, uint32_t value);
    uint16_t readRegister16(uint8_t reg);
    uint8_t readRegisterB(uint8_t reg);
    void writeRegisterB(uint8_t reg, uint8_t value);
    void directCommand(uint8_t command);
    void directCommand(uint8_t command, const uint8_t* data, std::size_t size);
    void modifyRegister(uint8_t reg, uint8_t setMask, uint8_t clearMask);

    void clearInterrupts();
    uint32_t readInterrupts();
    uint32_t waitForInterrupt(uint32_t flags, std::chrono::milliseconds timeout, const CancellationToken& cancellation);
    void configureNfcA(const CancellationToken& cancellation);
    void configureNfcF(const CancellationToken& cancellation);
    void enableOscillator(const CancellationToken& cancellation);
    void enableField(const CancellationToken& cancellation);
    void resetNfcAField(const CancellationToken& cancellation);
    void resetNfcFField(const CancellationToken& cancellation);
    void ensureNfcAField(const CancellationToken& cancellation);
    void ensureNfcFField(const CancellationToken& cancellation);
    void prepareTransceive();
    void setNoResponseTimeout(std::chrono::milliseconds timeout);
    void logWakeupDiagnostics(uint32_t flags, const CancellationToken& cancellation);

    WakeupResult requestWakeup(std::array<uint8_t, 2>& atqa, const CancellationToken& cancellation);
    bool anticollisionLevel(uint8_t level, std::array<uint8_t, 5>& response, const CancellationToken& cancellation);
    bool selectLevel(uint8_t level, const std::array<uint8_t, 5>& anticollision, uint8_t& sak,
                     const CancellationToken& cancellation);
    bool haltSelectedTag(const CancellationToken& cancellation);
    bool transceive(const uint8_t* transmit, std::size_t transmitSize, uint8_t* receive, std::size_t& receiveSize,
                    std::chrono::milliseconds timeout, std::size_t minimumReceive,
                    const CancellationToken& cancellation);
    bool readFifo(uint8_t* values, std::size_t capacity, std::size_t& actual);
    std::size_t fifoSize();
    void writeFifo(const uint8_t* values, std::size_t size);
    void setTransmitLength(std::size_t bytes, uint8_t trailingBits = 0);
    bool readType2Ndef(St25r3916NfcATag& tag, const CancellationToken& cancellation);
    bool readType2Block(uint8_t page, std::array<uint8_t, 16>& block, const CancellationToken& cancellation);
};

}  // namespace cap_nfc::nfc

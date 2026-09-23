#include "nfc/st25r3916_driver.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using cap_nfc::nfc::CancellationToken;
using cap_nfc::nfc::St25r3916Driver;
using cap_nfc::nfc::St25r3916DriverConfig;
using cap_nfc::nfc::St25r3916NfcAPollKind;
using cap_nfc::nfc::St25r3916NfcFPollKind;
using cap_nfc::nfc::St25r3916Transport;

class FakeTransport final : public St25r3916Transport {
public:
    bool tagPresent                               = true;
    bool selectReceiveStartOnly                   = false;
    bool wupaReceiveStartOnly                     = false;
    bool wupaRxError                              = false;
    bool wupaShortResponse                        = false;
    bool failHalt                                 = false;
    bool boundaryTlv                              = false;
    bool reportTransmitterOn                      = false;
    bool felicaTagPresent                         = false;
    bool felicaReceiveStartOnly                   = false;
    bool felicaNoResponseWithRxs                  = false;
    bool felicaShortResponse                      = false;
    bool felicaCollision                          = false;
    bool felicaNoReceiveInterrupt                 = false;
    bool felicaNoResponseInterrupts               = false;
    bool felicaDelayResponseUntilSecondFifoStatus = false;
    std::size_t type2ReadCount                    = 0;
    std::size_t haltCount                         = 0;
    std::size_t fieldResetCount                   = 0;
    std::size_t wupaWhileActive                   = 0;
    std::size_t sensfRequestCount                 = 0;
    std::size_t felicaFifoStatusReads             = 0;
    std::vector<std::vector<uint8_t>> directCommands;
    std::vector<uint16_t> wupaTransmitLengths;
    std::vector<uint8_t> wupaIsoSettings;
    std::vector<std::vector<uint8_t>> felicaRequests;

    FakeTransport()
    {
        _registers[0x3F]  = 0x2B;
        _registers[0x25]  = 0xA4;
        _registersB[0x2C] = 0xB2;
    }

    void transfer(const uint8_t* transmit, uint8_t* receive, std::size_t size) override
    {
        if (!transmit || !receive || size == 0) {
            throw std::runtime_error("invalid fake transfer");
        }
        std::fill(receive, receive + size, 0);

        const uint8_t operation = transmit[0];
        if (operation == 0xFB) {
            transferSpaceB(transmit, receive, size);
            return;
        }
        if (operation == 0x80) {
            _fifo.assign(transmit + 1, transmit + size);
            return;
        }
        if (operation == 0x9F) {
            const std::size_t count = std::min(size - 1, _fifo.size());
            std::copy_n(_fifo.begin(), count, receive + 1);
            _fifo.erase(_fifo.begin(), _fifo.begin() + count);
            return;
        }
        if ((operation & 0xC0) == 0x40) {
            readRegisters(static_cast<uint8_t>(operation & 0x3F), receive + 1, size - 1);
            return;
        }
        if ((operation & 0xC0) == 0x00) {
            writeRegisters(operation, transmit + 1, size - 1);
            return;
        }
        handleCommand(operation, transmit + 1, size - 1);
    }

    bool interruptAsserted() const override
    {
        return _mainInterrupt != 0 || _timerInterrupt != 0 || _errorInterrupt != 0 || _passiveInterrupt != 0;
    }

    bool waitForInterrupt(std::chrono::milliseconds, const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        return interruptAsserted();
    }

    void sleepFor(std::chrono::milliseconds, const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
    }

    uint8_t registerValue(uint8_t reg) const
    {
        return _registers[reg];
    }

    uint8_t registerBValue(uint8_t reg) const
    {
        return _registersB[reg];
    }

    void setRegisterValue(uint8_t reg, uint8_t value)
    {
        _registers[reg] = value;
    }

private:
    std::array<uint8_t, 64> _registers{};
    std::array<uint8_t, 64> _registersB{};
    std::vector<uint8_t> _fifo;
    uint8_t _mainInterrupt      = 0;
    uint8_t _timerInterrupt     = 0;
    uint8_t _errorInterrupt     = 0;
    uint8_t _passiveInterrupt   = 0;
    bool _tagActive             = false;
    bool _felicaResponsePending = false;

    void transferSpaceB(const uint8_t* transmit, uint8_t* receive, std::size_t size)
    {
        if (size < 3) {
            throw std::runtime_error("short fake Space-B transfer");
        }
        const uint8_t operation = transmit[1];
        const uint8_t reg       = static_cast<uint8_t>(operation & 0x3F);
        if ((operation & 0xC0) == 0x40) {
            receive[2] = _registersB[reg];
        } else {
            _registersB[reg] = transmit[2];
        }
    }

    void readRegisters(uint8_t reg, uint8_t* values, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i) {
            const uint8_t address = static_cast<uint8_t>((reg + i) & 0x3F);
            if (address == 0x1E) {
                ++felicaFifoStatusReads;
                if (_felicaResponsePending && felicaFifoStatusReads >= 2) {
                    populateFelicaResponse();
                }
                values[i] = static_cast<uint8_t>(_fifo.size() & 0xFF);
            } else if (address == 0x1F) {
                values[i] = static_cast<uint8_t>((_fifo.size() >> 2) & 0xC0);
            } else if (address == 0x1A) {
                values[i]      = _mainInterrupt;
                _mainInterrupt = 0;
            } else if (address == 0x1B) {
                values[i]       = _timerInterrupt;
                _timerInterrupt = 0;
            } else if (address == 0x1C) {
                values[i]       = _errorInterrupt;
                _errorInterrupt = 0;
            } else if (address == 0x1D) {
                values[i]         = _passiveInterrupt;
                _passiveInterrupt = 0;
            } else {
                values[i] = _registers[address];
            }
        }
    }

    void writeRegisters(uint8_t reg, const uint8_t* values, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i) {
            const uint8_t address = static_cast<uint8_t>((reg + i) & 0x3F);
            _registers[address]   = values[i];
            if (address == 0x02) {
                if (_tagActive && (values[i] & 0x08) == 0) {
                    _tagActive = false;
                    ++fieldResetCount;
                }
                _registers[0x31] = static_cast<uint8_t>(_registers[0x31] & ~(0x20 | 0x08));
                if ((values[i] & 0x80) != 0) {
                    _registers[0x31] |= 0x10;
                    _mainInterrupt |= 0x80;
                }
                if (reportTransmitterOn && (values[i] & 0x08) != 0) {
                    _registers[0x31] |= 0x20;
                }
                if ((values[i] & 0x40) != 0) {
                    _registers[0x31] |= 0x08;
                }
            }
        }
    }

    void handleCommand(uint8_t command, const uint8_t* data, std::size_t size)
    {
        directCommands.emplace_back(1, command);
        directCommands.back().insert(directCommands.back().end(), data, data + size);
        switch (command) {
            case 0xC1:
                resetChip();
                break;
            case 0xC4:
                respondToFrame();
                break;
            case 0xC5:
                respondToAnticollision();
                break;
            case 0xC7:
                wupaTransmitLengths.push_back(
                    static_cast<uint16_t>((static_cast<uint16_t>(_registers[0x22]) << 8) | _registers[0x23]));
                wupaIsoSettings.push_back(_registers[0x05]);
                if (tagPresent && !_tagActive) {
                    if (wupaRxError) {
                        _errorInterrupt |= 0x80;
                    } else if (wupaReceiveStartOnly) {
                        _fifo = {0x44};
                        _mainInterrupt |= 0x20;
                    } else {
                        _fifo = wupaShortResponse ? std::vector<uint8_t>{0x44} : std::vector<uint8_t>{0x44, 0x00};
                        _mainInterrupt |= 0x10;
                    }
                } else {
                    wupaWhileActive += _tagActive ? 1 : 0;
                    if (!tagPresent) {
                        _tagActive = false;
                    }
                    _timerInterrupt |= 0x40;
                }
                break;
            case 0xC8:
                _registers[0x02] |= 0x08;
                if (reportTransmitterOn) {
                    _registers[0x31] |= 0x20;
                }
                _timerInterrupt |= 0x02;
                _passiveInterrupt |= 0x20;
                break;
            case 0xDB:
                _fifo.clear();
                break;
            default:
                break;
        }
    }

    void resetChip()
    {
        const uint8_t identity = _registers[0x3F];
        const uint8_t adc      = _registers[0x25];
        _registers.fill(0);
        _registers[0x3F] = identity;
        _registers[0x25] = adc;
        _fifo.clear();
        _tagActive     = false;
        _mainInterrupt = _timerInterrupt = _errorInterrupt = _passiveInterrupt = 0;
    }

    void respondToAnticollision()
    {
        if (_fifo.size() < 2 || _fifo[0] != 0x93 || _fifo[1] != 0x20) {
            return;
        }
        constexpr std::array<uint8_t, 4> uid{0x04, 0xA1, 0xB2, 0xC3};
        const uint8_t bcc = static_cast<uint8_t>(uid[0] ^ uid[1] ^ uid[2] ^ uid[3]);
        _fifo.assign(uid.begin(), uid.end());
        _fifo.push_back(bcc);
        _mainInterrupt |= 0x10;
    }

    void respondToFrame()
    {
        if (_registers[0x03] == 0x1C && _fifo == std::vector<uint8_t>({0x00, 0xFF, 0xFF, 0x00, 0x03})) {
            ++sensfRequestCount;
            felicaRequests.push_back(_fifo);
            if (felicaTagPresent) {
                felicaFifoStatusReads = 0;
                if (felicaDelayResponseUntilSecondFifoStatus) {
                    _felicaResponsePending = true;
                } else {
                    populateFelicaResponse();
                }
                _mainInterrupt |= 0x08;  // TX end.
                if (!felicaNoReceiveInterrupt) {
                    if (felicaCollision) {
                        _mainInterrupt |= 0x04;  // Collision.
                    } else if (felicaReceiveStartOnly) {
                        _mainInterrupt |= 0x20;  // RX start; RX end is omitted.
                    } else {
                        _mainInterrupt |= 0x10;  // RX end.
                    }
                }
                if (felicaNoResponseWithRxs) {
                    _timerInterrupt |= 0x40;  // NRE can be latched with RXS.
                }
            } else {
                _felicaResponsePending = false;
                if (!felicaNoResponseInterrupts) {
                    _timerInterrupt |= 0x40;  // No response.
                    _mainInterrupt |= 0x08;   // TX end still completes.
                }
            }
            return;
        }
        if (_fifo.size() == 2 && _fifo[0] == 0x50 && _fifo[1] == 0x00) {
            ++haltCount;
            if (failHalt) {
                return;
            }
            _tagActive = false;
            _fifo.clear();
            _mainInterrupt |= 0x08;
            return;
        }
        if (_fifo.size() >= 2 && _fifo[0] == 0x93 && _fifo[1] == 0x70) {
            _fifo      = {0x00, 0x00, 0x00};
            _tagActive = true;
            _mainInterrupt |= selectReceiveStartOnly ? 0x20 : 0x10;
            return;
        }
        if (_fifo.size() == 2 && _fifo[0] == 0x30) {
            ++type2ReadCount;
            const uint8_t page = _fifo[1];
            _fifo.assign(16, 0);
            if (page == 0) {
                _fifo[12] = 0xE1;
                _fifo[13] = 0x10;
                _fifo[14] = boundaryTlv ? 0x02 : 0x12;
                _fifo[15] = 0x00;
            } else if (page == 4) {
                if (boundaryTlv) {
                    _fifo.back() = 0x01;
                } else {
                    const std::array<uint8_t, 12> ndef{0x03, 0x09, 0xD1, 0x01, 0x05, 0x54,
                                                       0x02, 0x65, 0x6E, 0x48, 0x69, 0xFE};
                    std::copy(ndef.begin(), ndef.end(), _fifo.begin());
                }
            }
            _mainInterrupt |= 0x10;
        }
    }

    void populateFelicaResponse()
    {
        _fifo = {0x12, 0x01, 0x01, 0xFE, 0x12, 0x34, 0x56, 0x78, 0x9A,
                 0xBC, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        if (felicaShortResponse) {
            _fifo.resize(8);
        }
        _felicaResponsePending = false;
    }
};

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testInitializationAndType2Poll()
{
    std::atomic_bool cancelled{false};
    CancellationToken cancellation(cancelled);
    FakeTransport transport;
    St25r3916Driver driver(transport);

    const auto chip = driver.initialize(cancellation);
    require(chip.identity == 0x2B, "identity mismatch");
    require(chip.revision == 3, "revision mismatch");
    require((transport.registerValue(0x01) & 0x80) == 0, "sup3V must remain clear for the Cap's 5 V VDD");
    require((transport.registerValue(0x01) & 0x18) == 0x18,
            "both MISO pull-downs must be enabled for stable translated-bus direction switching");
    require((transport.registerValue(0x01) & 0x24) == 0x24,
            "AAT and increased MISO drive must be configured without a read-modify-write");
    require(transport.registerBValue(0x15) == 33, "NFC field-on guard timer must be written in register Space B");
    require(transport.registerValue(0x15) == 0, "NFC field-on setup must not overwrite the Space-A PPON2 timer");
    require(transport.registerValue(0x03) == 0x08,
            "NFC-A reader mode must not enable automatic response field-on behavior");
    require((transport.registerValue(0x02) & 0x48) == 0x48, "NFC field and receiver must remain enabled");
    require((transport.registerValue(0x31) & 0x20) == 0, "the fake must exercise idle AUX_DISPLAY.tx_on being clear");
    require(driver.ready(), "driver must be ready after initialization");
    const auto setDefault =
        std::find(transport.directCommands.begin(), transport.directCommands.end(), std::vector<uint8_t>{0xC1});
    require(setDefault != transport.directCommands.end(), "Set Default command missing");
    require(std::next(setDefault) != transport.directCommands.end() &&
                *std::next(setDefault) == std::vector<uint8_t>({0xFC, 0x04, 0x10}),
            "protection errata must immediately follow Set Default");
    require(std::find(std::next(setDefault), transport.directCommands.end(), std::vector<uint8_t>{0xC1}) ==
                transport.directCommands.end(),
            "unexpected additional Set Default command");

    const auto poll = driver.pollNfcA(cancellation);
    require(poll.kind == St25r3916NfcAPollKind::Tag && poll.tag, "expected a tag");
    const auto& tag = *poll.tag;
    require(tag.uid == std::vector<uint8_t>({0x04, 0xA1, 0xB2, 0xC3}), "UID mismatch");
    require(tag.atqa == std::array<uint8_t, 2>({0x44, 0x00}), "ATQA mismatch");
    require(tag.sak == 0x00, "SAK mismatch");
    require(tag.ndefSupported && tag.ndefReadable, "Type 2 NDEF capability missing");
    require(tag.ndefCapacity == 144, "NDEF capacity mismatch");
    require(tag.ndefMessage == std::vector<uint8_t>({0xD1, 0x01, 0x05, 0x54, 0x02, 0x65, 0x6E, 0x48, 0x69}),
            "NDEF message mismatch");
    const std::size_t firstReadCount = transport.type2ReadCount;
    require(driver.pollNfcA(cancellation).kind == St25r3916NfcAPollKind::Tag, "expected the same tag on a second poll");
    require(transport.type2ReadCount == firstReadCount, "NDEF should be cached while the same tag remains present");
    require(transport.haltCount >= 2, "each completed discovery cycle must place the selected tag in HALT");
    require(transport.wupaWhileActive == 0, "WUPA must not be sent while the previous selection is still active");
    require(transport.wupaTransmitLengths.size() >= 2,
            "the test must exercise WUPA after a previous anticollision/select transaction");
    require(std::all_of(transport.wupaTransmitLengths.begin(), transport.wupaTransmitLengths.end(),
                        [](uint16_t length) { return length == 0; }),
            "WUPA must clear the previous transmit length before issuing the direct command");
    require(std::all_of(transport.wupaIsoSettings.begin(), transport.wupaIsoSettings.end(),
                        [](uint8_t settings) { return (settings & 0x01) == 0; }),
            "WUPA must use standard-frame receive settings rather than bit-oriented anticollision mode");

    transport.setRegisterValue(0x02, 0xCC);
    require(driver.pollNfcA(cancellation).kind == St25r3916NfcAPollKind::Tag,
            "the driver must recover a drifted NFC-A field state");
    require((transport.registerValue(0x02) & 0xCF) == 0xC9, "field recovery must restore Reader operation state");

    transport.tagPresent = false;
    require(driver.pollNfcA(cancellation).kind == St25r3916NfcAPollKind::NoTag,
            "expected an explicit no-tag result after removal");
    driver.stop();
    require(!driver.ready(), "driver must stop");
    require((transport.registerValue(0x02) & 0x48) == 0,
            "exit must disable RF transmitter and receiver while the Cap stays powered");
}

void testReceiveStartIsNotACompleteFrame()
{
    std::atomic_bool cancelled{false};
    CancellationToken cancellation(cancelled);
    FakeTransport transport;
    transport.selectReceiveStartOnly = true;
    St25r3916Driver driver(transport);

    (void)driver.initialize(cancellation);
    const auto poll = driver.pollNfcA(cancellation);
    require(poll.kind == St25r3916NfcAPollKind::Inconclusive && !poll.tag,
            "RX start without RX end must be inconclusive rather than explicit absence");

    transport.selectReceiveStartOnly = false;
    const auto recovered             = driver.pollNfcA(cancellation);
    require(recovered.kind == St25r3916NfcAPollKind::Tag && recovered.tag,
            "the poll after an incomplete SELECT must recover the card with a field reset");
    require(transport.fieldResetCount >= 1, "an incomplete SELECT must force the NFC-A field off before retrying");
    require(transport.wupaWhileActive == 0, "recovery must not send WUPA while the card remains active");
}

void testMisoPullConfiguration()
{
    std::atomic_bool cancelled{false};
    CancellationToken cancellation(cancelled);
    FakeTransport transport;
    St25r3916DriverConfig config;
    config.misoPullWhenDeselected = false;
    config.misoPullWhenSelected   = false;
    St25r3916Driver driver(transport, config);

    (void)driver.initialize(cancellation);
    require((transport.registerValue(0x01) & 0x18) == 0,
            "diagnostic configuration must be able to disable the ST25R3916 MISO pull-downs");
}

void testWakeupClassification()
{
    const auto pollOnce = [](auto configure) {
        std::atomic_bool cancelled{false};
        CancellationToken cancellation(cancelled);
        FakeTransport transport;
        configure(transport);
        St25r3916Driver driver(transport);
        (void)driver.initialize(cancellation);
        return driver.pollNfcA(cancellation);
    };

    require(
        pollOnce([](FakeTransport& transport) { transport.tagPresent = false; }).kind == St25r3916NfcAPollKind::NoTag,
        "a clean WUPA no-response interrupt must report explicit absence");
    require(pollOnce([](FakeTransport& transport) { transport.wupaReceiveStartOnly = true; }).kind ==
                St25r3916NfcAPollKind::Inconclusive,
            "WUPA RX start without a complete frame must be inconclusive");
    require(pollOnce([](FakeTransport& transport) { transport.wupaRxError = true; }).kind ==
                St25r3916NfcAPollKind::Inconclusive,
            "a WUPA receive error must be inconclusive");
    require(pollOnce([](FakeTransport& transport) { transport.wupaShortResponse = true; }).kind ==
                St25r3916NfcAPollKind::Inconclusive,
            "a short WUPA response must be inconclusive");
}

void testFailedHaltAndRemovalInvalidateState()
{
    std::atomic_bool cancelled{false};
    CancellationToken cancellation(cancelled);
    FakeTransport transport;
    transport.failHalt = true;
    St25r3916Driver driver(transport);
    (void)driver.initialize(cancellation);

    const auto first = driver.pollNfcA(cancellation);
    require(first.kind == St25r3916NfcAPollKind::Tag && first.tag,
            "a recognized tag must still be reported when HLTA transmission is inconclusive");
    const std::size_t firstReadCount = transport.type2ReadCount;

    transport.failHalt   = false;
    const auto recovered = driver.pollNfcA(cancellation);
    require(recovered.kind == St25r3916NfcAPollKind::Tag && recovered.tag,
            "a failed HLTA must be recovered with a field reset before the next WUPA");
    require(transport.fieldResetCount >= 1, "a failed HLTA must force the NFC-A field off before retrying");
    require(transport.wupaWhileActive == 0, "HLTA recovery must not issue WUPA while the card remains active");

    transport.tagPresent = false;
    for (std::size_t index = 0; index < 3; ++index) {
        require(driver.pollNfcA(cancellation).kind == St25r3916NfcAPollKind::NoTag,
                "clean removal polls must report explicit absence");
    }
    transport.tagPresent = true;
    require(driver.pollNfcA(cancellation).kind == St25r3916NfcAPollKind::Tag,
            "the same UID must be readable after confirmed removal");
    require(transport.type2ReadCount > firstReadCount,
            "confirmed removal must invalidate cached NDEF data for a later presentation of the same UID");
}

void testBoundaryTlvDoesNotReadPastCapacity()
{
    std::atomic_bool cancelled{false};
    CancellationToken cancellation(cancelled);
    FakeTransport transport;
    transport.boundaryTlv = true;
    St25r3916Driver driver(transport);

    (void)driver.initialize(cancellation);
    const auto poll = driver.pollNfcA(cancellation);
    require(poll.kind == St25r3916NfcAPollKind::Tag && poll.tag, "boundary TLV tag should still be identified");
    require(poll.tag->ndefSupported && poll.tag->ndefReadable, "boundary TLV capability should be reported");
    require(poll.tag->ndefMessage.empty(), "malformed boundary TLV must not produce an NDEF message");
}

void testNfcFPollAndProtocolSwitch()
{
    std::atomic_bool cancelled{false};
    CancellationToken cancellation(cancelled);
    FakeTransport transport;
    transport.felicaTagPresent = true;
    St25r3916Driver driver(transport);

    (void)driver.initialize(cancellation);
    const auto poll = driver.pollNfcF(cancellation);
    require(poll.kind == St25r3916NfcFPollKind::Tag && poll.tag, "expected an NFC-F tag");
    const auto& tag = *poll.tag;
    require(tag.idm == std::array<uint8_t, 8>({0x01, 0xFE, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}), "NFC-F IDm mismatch");
    require(tag.pmm == std::array<uint8_t, 8>({0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}), "NFC-F PMm mismatch");
    require(tag.typeName == "NFC-F / FeliCa (NFC Forum Type 3)", "NFC-F type name mismatch");
    require(transport.registerValue(0x03) == 0x1C, "NFC-F initiator mode must be configured");
    require(transport.registerValue(0x04) == 0x11, "NFC-F bitrate must be 212 kbps in both directions");
    require(transport.registerValue(0x07) == 0x00, "FeliCa settings must be reset for polling");
    require((transport.registerValue(0x12) & 0x02) != 0,
            "NFC-F polling must keep the no-response timer running across receive slots");
    require(transport.registerValue(0x0B) == 0x13 && transport.registerValue(0x0C) == 0x3D &&
                transport.registerValue(0x0D) == 0x00 && transport.registerValue(0x0E) == 0x00,
            "NFC-F receiver configuration mismatch");
    require(transport.registerBValue(0x0C) == 0x54 && transport.registerBValue(0x0D) == 0x00,
            "NFC-F correlator configuration mismatch");
    require(transport.sensfRequestCount == 1, "expected one SENSF_REQ");
    require(transport.felicaRequests.front() == std::vector<uint8_t>({0x00, 0xFF, 0xFF, 0x00, 0x03}),
            "SENSF_REQ must omit the on-air length byte and request four time slots");

    // Switching back to A must reapply the A profile rather than leaving the
    // chip in FeliCa mode for the next discovery cycle.
    transport.felicaTagPresent = false;
    const auto aPoll           = driver.pollNfcA(cancellation);
    require(aPoll.kind == St25r3916NfcAPollKind::Tag && aPoll.tag, "switching back to NFC-A must recover the tag");
    require(transport.registerValue(0x03) == 0x08, "NFC-A mode must be restored after NFC-F polling");
    require((transport.registerValue(0x12) & 0x02) == 0, "NFC-A mode must clear NFC-F's no-response timer handling");
    driver.startNfcF(cancellation);
    cancelled.store(true);
    driver.stop();
    require(!driver.ready() && !driver.discoveryEnabled(), "cancelled NFC-F scanning must stop on exit");
    require((transport.registerValue(0x02) & 0x48) == 0, "NFC-F exit must disable RF with the Cap still powered");
}

void testNfcFNoTagClassification()
{
    std::atomic_bool cancelled{false};
    CancellationToken cancellation(cancelled);
    FakeTransport transport;
    transport.felicaTagPresent = false;
    St25r3916Driver driver(transport);
    (void)driver.initialize(cancellation);

    const auto poll = driver.pollNfcF(cancellation);
    require(poll.kind == St25r3916NfcFPollKind::NoTag && !poll.tag,
            "a clean NFC-F no-response interrupt must report explicit absence");
}

void testNfcFAcceptsCompleteFifoWithoutRxEnd()
{
    std::atomic_bool cancelled{false};
    CancellationToken cancellation(cancelled);
    FakeTransport transport;
    transport.felicaTagPresent       = true;
    transport.felicaReceiveStartOnly = true;
    St25r3916Driver driver(transport);
    (void)driver.initialize(cancellation);

    const auto poll = driver.pollNfcF(cancellation);
    require(poll.kind == St25r3916NfcFPollKind::Tag && poll.tag,
            "a complete NFC-F FIFO response must be accepted even without RX end");
}

void testNfcFAcceptsCompleteFifoWhenNoResponseSharesRxStart()
{
    std::atomic_bool cancelled{false};
    CancellationToken cancellation(cancelled);
    FakeTransport transport;
    transport.felicaTagPresent        = true;
    transport.felicaReceiveStartOnly  = true;
    transport.felicaNoResponseWithRxs = true;
    St25r3916Driver driver(transport);
    (void)driver.initialize(cancellation);

    const auto poll = driver.pollNfcF(cancellation);
    require(poll.kind == St25r3916NfcFPollKind::Tag && poll.tag,
            "an NFC-F response with simultaneous NRE/RXS must not be discarded");
}

void testNfcFAcceptsCompleteFifoWithoutReceiveInterrupt()
{
    std::atomic_bool cancelled{false};
    CancellationToken cancellation(cancelled);
    FakeTransport transport;
    transport.felicaTagPresent         = true;
    transport.felicaNoReceiveInterrupt = true;
    St25r3916Driver driver(transport);
    (void)driver.initialize(cancellation);

    const auto poll = driver.pollNfcF(cancellation);
    require(poll.kind == St25r3916NfcFPollKind::Tag && poll.tag,
            "a complete FIFO response must be accepted when RX interrupts are missing");
}

void testNfcFAcceptsResponseWhenFifoLagsRxEnd()
{
    std::atomic_bool cancelled{false};
    CancellationToken cancellation(cancelled);
    FakeTransport transport;
    transport.felicaTagPresent                         = true;
    transport.felicaDelayResponseUntilSecondFifoStatus = true;
    St25r3916Driver driver(transport);
    (void)driver.initialize(cancellation);

    const auto poll = driver.pollNfcF(cancellation);
    require(poll.kind == St25r3916NfcFPollKind::Tag && poll.tag,
            "a FIFO response that arrives just after RX end must still be accepted");
    require(transport.felicaFifoStatusReads >= 2, "the driver must perform a final FIFO status sample after RX end");
}

void testNfcFRejectsCollisionAsInconclusive()
{
    std::atomic_bool cancelled{false};
    CancellationToken cancellation(cancelled);
    FakeTransport transport;
    transport.felicaTagPresent = true;
    transport.felicaCollision  = true;
    St25r3916Driver driver(transport);
    (void)driver.initialize(cancellation);

    const auto poll = driver.pollNfcF(cancellation);
    require(poll.kind == St25r3916NfcFPollKind::Inconclusive && !poll.tag,
            "a colliding NFC-F response must not be published as a tag");
}

void testNfcFRejectsShortResponse()
{
    std::atomic_bool cancelled{false};
    CancellationToken cancellation(cancelled);
    FakeTransport transport;
    transport.felicaTagPresent    = true;
    transport.felicaShortResponse = true;
    St25r3916Driver driver(transport);
    (void)driver.initialize(cancellation);

    const auto poll = driver.pollNfcF(cancellation);
    require(poll.kind == St25r3916NfcFPollKind::Inconclusive && !poll.tag,
            "a short NFC-F response must remain inconclusive");
}

void testNfcFInfersNoTagWhenInterruptsAreMissing()
{
    std::atomic_bool cancelled{false};
    CancellationToken cancellation(cancelled);
    FakeTransport transport;
    transport.felicaNoResponseInterrupts = true;
    St25r3916Driver driver(transport);
    (void)driver.initialize(cancellation);

    const auto poll = driver.pollNfcF(cancellation);
    require(poll.kind == St25r3916NfcFPollKind::NoTag && !poll.tag,
            "an empty FIFO at the polling deadline must infer no tag when IRQs are lost");
}

}  // namespace

int main()
{
    try {
        testInitializationAndType2Poll();
        testReceiveStartIsNotACompleteFrame();
        testMisoPullConfiguration();
        testWakeupClassification();
        testFailedHaltAndRemovalInvalidateState();
        testBoundaryTlvDoesNotReadPastCapacity();
        testNfcFPollAndProtocolSwitch();
        testNfcFNoTagClassification();
        testNfcFAcceptsCompleteFifoWithoutRxEnd();
        testNfcFAcceptsCompleteFifoWhenNoResponseSharesRxStart();
        testNfcFAcceptsCompleteFifoWithoutReceiveInterrupt();
        testNfcFAcceptsResponseWhenFifoLagsRxEnd();
        testNfcFRejectsCollisionAsInconclusive();
        testNfcFRejectsShortResponse();
        testNfcFInfersNoTagWhenInterruptsAreMissing();
        std::cout << "ST25R3916 driver tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ST25R3916 driver test failed: " << exception.what() << '\n';
        return 1;
    }
}

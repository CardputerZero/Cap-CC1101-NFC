// SPDX-License-Identifier: MIT
// Copyright (c) 2026 M5Stack CardputerZero Community

#include "nfc/ndef_parser.hpp"

#include <array>
#include <limits>
#include <string>
#include <utility>

namespace cap_nfc::nfc {
namespace {

constexpr uint8_t kMessageBegin = 0x80;
constexpr uint8_t kMessageEnd   = 0x40;
constexpr uint8_t kChunkFlag    = 0x20;
constexpr uint8_t kShortRecord  = 0x10;
constexpr uint8_t kIdLengthFlag = 0x08;
constexpr uint8_t kTnfMask      = 0x07;

constexpr uint8_t kTnfEmpty     = 0x00;
constexpr uint8_t kTnfWellKnown = 0x01;
constexpr uint8_t kTnfMime      = 0x02;
constexpr uint8_t kTnfExternal  = 0x04;
constexpr uint8_t kTnfUnknown   = 0x05;
constexpr uint8_t kTnfUnchanged = 0x06;
constexpr uint8_t kTnfReserved  = 0x07;

constexpr std::array<const char*, 36> kUriPrefixes = {
    "",                            // 0x00
    "http://www.",                 // 0x01
    "https://www.",                // 0x02
    "http://",                     // 0x03
    "https://",                    // 0x04
    "tel:",                        // 0x05
    "mailto:",                     // 0x06
    "ftp://anonymous:anonymous@",  // 0x07
    "ftp://ftp.",                  // 0x08
    "ftps://",                     // 0x09
    "sftp://",                     // 0x0A
    "smb://",                      // 0x0B
    "nfs://",                      // 0x0C
    "ftp://",                      // 0x0D
    "dav://",                      // 0x0E
    "news:",                       // 0x0F
    "telnet://",                   // 0x10
    "imap:",                       // 0x11
    "rtsp://",                     // 0x12
    "urn:",                        // 0x13
    "pop:",                        // 0x14
    "sip:",                        // 0x15
    "sips:",                       // 0x16
    "tftp:",                       // 0x17
    "btspp://",                    // 0x18
    "btl2cap://",                  // 0x19
    "btgoep://",                   // 0x1A
    "tcpobex://",                  // 0x1B
    "irdaobex://",                 // 0x1C
    "file://",                     // 0x1D
    "urn:epc:id:",                 // 0x1E
    "urn:epc:tag:",                // 0x1F
    "urn:epc:pat:",                // 0x20
    "urn:epc:raw:",                // 0x21
    "urn:epc:",                    // 0x22
    "urn:nfc:",                    // 0x23
};

class Cursor {
public:
    Cursor(const uint8_t* data, std::size_t size) : _data(data), _size(size)
    {
    }

    std::size_t remaining() const noexcept
    {
        return _size - _offset;
    }

    bool readByte(uint8_t& value) noexcept
    {
        if (remaining() < 1) {
            return false;
        }
        value = _data[_offset++];
        return true;
    }

    bool readBigEndianUint32(uint32_t& value) noexcept
    {
        if (remaining() < 4) {
            return false;
        }
        value = (static_cast<uint32_t>(_data[_offset]) << 24U) | (static_cast<uint32_t>(_data[_offset + 1]) << 16U) |
                (static_cast<uint32_t>(_data[_offset + 2]) << 8U) | static_cast<uint32_t>(_data[_offset + 3]);
        _offset += 4;
        return true;
    }

    bool readBytes(std::size_t length, const uint8_t*& bytes) noexcept
    {
        if (length > remaining()) {
            return false;
        }
        bytes = _data + _offset;
        _offset += length;
        return true;
    }

private:
    const uint8_t* _data;
    std::size_t _size;
    std::size_t _offset = 0;
};

NdefParseResult failure(std::string message)
{
    NdefParseResult result;
    result.error = std::move(message);
    return result;
}

bool isContinuation(uint8_t value)
{
    return (value & 0xC0U) == 0x80U;
}

bool isValidUtf8(const uint8_t* data, std::size_t size)
{
    std::size_t offset = 0;
    while (offset < size) {
        const uint8_t first = data[offset];
        if (first <= 0x7FU) {
            ++offset;
            continue;
        }

        if (first >= 0xC2U && first <= 0xDFU) {
            if (size - offset < 2 || !isContinuation(data[offset + 1])) {
                return false;
            }
            offset += 2;
            continue;
        }

        if (first >= 0xE0U && first <= 0xEFU) {
            if (size - offset < 3 || !isContinuation(data[offset + 1]) || !isContinuation(data[offset + 2])) {
                return false;
            }
            if ((first == 0xE0U && data[offset + 1] < 0xA0U) || (first == 0xEDU && data[offset + 1] >= 0xA0U)) {
                return false;
            }
            offset += 3;
            continue;
        }

        if (first >= 0xF0U && first <= 0xF4U) {
            if (size - offset < 4 || !isContinuation(data[offset + 1]) || !isContinuation(data[offset + 2]) ||
                !isContinuation(data[offset + 3])) {
                return false;
            }
            if ((first == 0xF0U && data[offset + 1] < 0x90U) || (first == 0xF4U && data[offset + 1] >= 0x90U)) {
                return false;
            }
            offset += 4;
            continue;
        }

        return false;
    }
    return true;
}

bool decodeRecord(uint8_t tnf, const uint8_t* type, std::size_t typeLength, const uint8_t* payload,
                  std::size_t payloadLength, NdefRecord& record, std::string& error)
{
    record.type.assign(reinterpret_cast<const char*>(type), typeLength);
    record.payload.assign(payload, payload + payloadLength);

    if (tnf == kTnfWellKnown && typeLength == 1 && type[0] == 'T') {
        if (payloadLength == 0) {
            error = "NDEF Text record has no status byte";
            return false;
        }

        const uint8_t status = payload[0];
        if ((status & 0x40U) != 0) {
            error = "NDEF Text record uses the reserved status bit";
            return false;
        }

        const std::size_t languageLength = status & 0x3FU;
        if (languageLength > payloadLength - 1) {
            error = "NDEF Text record language code exceeds its payload";
            return false;
        }

        // UTF-16 is a valid Text RTD encoding, but this lightweight parser only
        // decodes UTF-8. Keep unsupported Text records intact as raw records.
        if ((status & 0x80U) != 0) {
            return true;
        }

        const uint8_t* text       = payload + 1 + languageLength;
        const std::size_t textLen = payloadLength - 1 - languageLength;
        if (!isValidUtf8(text, textLen)) {
            error = "NDEF Text record contains invalid UTF-8";
            return false;
        }

        record.kind  = NdefRecordKind::Text;
        record.type  = "text/plain";
        record.value = std::string(reinterpret_cast<const char*>(text), textLen);
        return true;
    }

    if (tnf == kTnfWellKnown && typeLength == 1 && type[0] == 'U') {
        if (payloadLength == 0) {
            error = "NDEF URI record has no identifier code";
            return false;
        }

        const uint8_t identifier = payload[0];
        const char* prefix       = identifier < kUriPrefixes.size() ? kUriPrefixes[identifier] : "";
        record.kind              = NdefRecordKind::Uri;
        record.type              = "text/uri-list";
        record.value             = prefix;
        record.value.append(reinterpret_cast<const char*>(payload + 1), payloadLength - 1);
        return true;
    }

    if (tnf == kTnfMime) {
        record.kind  = NdefRecordKind::Mime;
        record.value = std::string(reinterpret_cast<const char*>(payload), payloadLength);
    }
    return true;
}

}  // namespace

NdefParseResult parseNdefMessage(const uint8_t* data, std::size_t size)
{
    if (size == 0) {
        return failure("NDEF message is empty");
    }
    if (data == nullptr) {
        return failure("NDEF message data is null");
    }

    Cursor cursor(data, size);
    NdefParseResult result;
    bool firstRecord = true;
    bool messageEnd  = false;

    while (cursor.remaining() != 0) {
        if (messageEnd) {
            return failure("NDEF message contains data after its ME record");
        }

        uint8_t header = 0;
        if (!cursor.readByte(header)) {
            return failure("NDEF record header is truncated");
        }

        const bool messageBegin = (header & kMessageBegin) != 0;
        const bool recordEnd    = (header & kMessageEnd) != 0;
        const bool chunked      = (header & kChunkFlag) != 0;
        const bool shortRecord  = (header & kShortRecord) != 0;
        const bool hasIdLength  = (header & kIdLengthFlag) != 0;
        const uint8_t tnf       = header & kTnfMask;

        if (firstRecord != messageBegin) {
            return failure(firstRecord ? "First NDEF record is missing MB"
                                       : "NDEF record after the first record sets MB");
        }
        if (chunked) {
            return failure("Chunked NDEF records are not supported");
        }
        if (tnf == kTnfUnchanged) {
            return failure("TNF Unchanged is only valid in chunked NDEF records");
        }
        if (tnf == kTnfReserved) {
            return failure("NDEF record uses reserved TNF");
        }

        uint8_t typeLength = 0;
        if (!cursor.readByte(typeLength)) {
            return failure("NDEF record is missing its type length");
        }

        uint32_t payloadLength32 = 0;
        if (shortRecord) {
            uint8_t shortPayloadLength = 0;
            if (!cursor.readByte(shortPayloadLength)) {
                return failure("Short NDEF record is missing its payload length");
            }
            payloadLength32 = shortPayloadLength;
        } else if (!cursor.readBigEndianUint32(payloadLength32)) {
            return failure("NDEF record has a truncated 32-bit payload length");
        }

        uint8_t idLength = 0;
        if (hasIdLength && !cursor.readByte(idLength)) {
            return failure("NDEF record is missing its ID length");
        }

        if (static_cast<uintmax_t>(payloadLength32) > std::numeric_limits<std::size_t>::max()) {
            return failure("NDEF payload length is unsupported on this platform");
        }
        const std::size_t payloadLength = static_cast<std::size_t>(payloadLength32);
        if (tnf == kTnfEmpty && (typeLength != 0 || idLength != 0 || payloadLength != 0)) {
            return failure("TNF Empty record contains a type, ID, or payload");
        }
        if (tnf >= kTnfWellKnown && tnf <= kTnfExternal && typeLength == 0) {
            return failure("Typed NDEF record has an empty type");
        }
        if (tnf == kTnfUnknown && typeLength != 0) {
            return failure("TNF Unknown record contains a type");
        }

        const uint8_t* type    = nullptr;
        const uint8_t* id      = nullptr;
        const uint8_t* payload = nullptr;
        if (!cursor.readBytes(typeLength, type)) {
            return failure("NDEF record type exceeds the message boundary");
        }
        if (!cursor.readBytes(idLength, id)) {
            return failure("NDEF record ID exceeds the message boundary");
        }
        if (!cursor.readBytes(payloadLength, payload)) {
            return failure("NDEF record payload exceeds the message boundary");
        }
        (void)id;

        NdefRecord record;
        std::string decodeError;
        if (!decodeRecord(tnf, type, typeLength, payload, payloadLength, record, decodeError)) {
            return failure(std::move(decodeError));
        }
        result.records.push_back(std::move(record));

        firstRecord = false;
        messageEnd  = recordEnd;
    }

    if (!messageEnd) {
        return failure("NDEF message is missing its ME record");
    }

    result.success = true;
    return result;
}

NdefParseResult parseNdefMessage(const std::vector<uint8_t>& message)
{
    return parseNdefMessage(message.data(), message.size());
}

}  // namespace cap_nfc::nfc

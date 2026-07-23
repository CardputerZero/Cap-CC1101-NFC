// SPDX-License-Identifier: MIT
// Copyright (c) 2026 M5Stack CardputerZero Community

#include "nfc/ndef_parser.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace cap_nfc::nfc;

#define CHECK(condition)                                                                         \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                                        \
        }                                                                                        \
    } while (false)

std::vector<uint8_t> textRecord(uint8_t header, const std::string& text)
{
    std::vector<uint8_t> message = {header, 0x01, static_cast<uint8_t>(3 + text.size()), 'T', 0x02, 'e', 'n'};
    message.insert(message.end(), text.begin(), text.end());
    return message;
}

bool parseFails(const std::vector<uint8_t>& message)
{
    const auto result = parseNdefMessage(message);
    return !result.success && !result.error.empty() && result.records.empty();
}

bool testTextRecord()
{
    const auto result = parseNdefMessage(textRecord(0xD1, "Hello, NFC"));
    CHECK(result.success);
    CHECK(result.error.empty());
    CHECK(result.records.size() == 1);
    CHECK(result.records[0].kind == NdefRecordKind::Text);
    CHECK(result.records[0].type == "text/plain");
    CHECK(result.records[0].value == "Hello, NFC");
    CHECK(result.records[0].payload ==
          std::vector<uint8_t>({0x02, 'e', 'n', 'H', 'e', 'l', 'l', 'o', ',', ' ', 'N', 'F', 'C'}));
    return true;
}

bool testTextRecordWithId()
{
    const std::vector<uint8_t> message = {
        0xD9, 0x01, 0x08, 0x02, 'T', 0x12, 0x34, 0x02, 'e', 'n', 'H', 'e', 'l', 'l', 'o',
    };
    const auto result = parseNdefMessage(message);
    CHECK(result.success);
    CHECK(result.records.size() == 1);
    CHECK(result.records[0].kind == NdefRecordKind::Text);
    CHECK(result.records[0].value == "Hello");
    return true;
}

bool testUriPrefixTable()
{
    constexpr std::array<const char*, 36> prefixes = {
        "",
        "http://www.",
        "https://www.",
        "http://",
        "https://",
        "tel:",
        "mailto:",
        "ftp://anonymous:anonymous@",
        "ftp://ftp.",
        "ftps://",
        "sftp://",
        "smb://",
        "nfs://",
        "ftp://",
        "dav://",
        "news:",
        "telnet://",
        "imap:",
        "rtsp://",
        "urn:",
        "pop:",
        "sip:",
        "sips:",
        "tftp:",
        "btspp://",
        "btl2cap://",
        "btgoep://",
        "tcpobex://",
        "irdaobex://",
        "file://",
        "urn:epc:id:",
        "urn:epc:tag:",
        "urn:epc:pat:",
        "urn:epc:raw:",
        "urn:epc:",
        "urn:nfc:",
    };

    for (std::size_t index = 0; index < prefixes.size(); ++index) {
        const std::vector<uint8_t> message = {0xD1, 0x01, 0x02, 'U', static_cast<uint8_t>(index), 'x'};
        const auto result                  = parseNdefMessage(message);
        CHECK(result.success);
        CHECK(result.records.size() == 1);
        CHECK(result.records[0].kind == NdefRecordKind::Uri);
        CHECK(result.records[0].type == "text/uri-list");
        CHECK(result.records[0].value == std::string(prefixes[index]) + "x");
        CHECK(result.records[0].payload == std::vector<uint8_t>({static_cast<uint8_t>(index), 'x'}));
    }
    return true;
}

bool testMimeRecord()
{
    const std::string mimeType   = "text/plain";
    const std::string body       = "hello";
    std::vector<uint8_t> message = {0xD2, static_cast<uint8_t>(mimeType.size()), static_cast<uint8_t>(body.size())};
    message.insert(message.end(), mimeType.begin(), mimeType.end());
    message.insert(message.end(), body.begin(), body.end());

    const auto result = parseNdefMessage(message);
    CHECK(result.success);
    CHECK(result.records.size() == 1);
    CHECK(result.records[0].kind == NdefRecordKind::Mime);
    CHECK(result.records[0].type == mimeType);
    CHECK(result.records[0].value == body);
    CHECK(result.records[0].payload == std::vector<uint8_t>(body.begin(), body.end()));
    return true;
}

bool testLongPayload()
{
    const std::string mimeType = "application/octet-stream";
    std::vector<uint8_t> payload(256);
    for (std::size_t index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<uint8_t>(index);
    }

    std::vector<uint8_t> message = {0xC2, static_cast<uint8_t>(mimeType.size()), 0x00, 0x00, 0x01, 0x00};
    message.insert(message.end(), mimeType.begin(), mimeType.end());
    message.insert(message.end(), payload.begin(), payload.end());

    const auto result = parseNdefMessage(message);
    CHECK(result.success);
    CHECK(result.records.size() == 1);
    CHECK(result.records[0].kind == NdefRecordKind::Mime);
    CHECK(result.records[0].payload == payload);
    return true;
}

bool testMultipleRecords()
{
    auto message      = textRecord(0x91, "First");
    const auto second = textRecord(0x51, "Second");
    message.insert(message.end(), second.begin(), second.end());

    const auto result = parseNdefMessage(message);
    CHECK(result.success);
    CHECK(result.records.size() == 2);
    CHECK(result.records[0].value == "First");
    CHECK(result.records[1].value == "Second");
    return true;
}

bool testUnknownRecordPreservesRawData()
{
    const std::vector<uint8_t> message = {
        0xD4, 0x0C, 0x03, 'e', 'x', 'a', 'm', 'p', 'l', 'e', ':', 't', 'y', 'p', 'e', 0x00, 0xA5, 0xFF,
    };
    const auto result = parseNdefMessage(message);
    CHECK(result.success);
    CHECK(result.records.size() == 1);
    CHECK(result.records[0].kind == NdefRecordKind::Unknown);
    CHECK(result.records[0].type == "example:type");
    CHECK(result.records[0].value.empty());
    CHECK(result.records[0].payload == std::vector<uint8_t>({0x00, 0xA5, 0xFF}));
    return true;
}

bool testMalformedMessages()
{
    CHECK(parseFails({}));
    const auto nullResult = parseNdefMessage(nullptr, 1);
    CHECK(!nullResult.success && !nullResult.error.empty());

    CHECK(parseFails(textRecord(0x51, "No MB")));
    CHECK(parseFails(textRecord(0x91, "No ME")));

    auto duplicateMb       = textRecord(0x91, "First");
    const auto finalWithMb = textRecord(0xD1, "Second");
    duplicateMb.insert(duplicateMb.end(), finalWithMb.begin(), finalWithMb.end());
    CHECK(parseFails(duplicateMb));

    auto trailing = textRecord(0xD1, "Done");
    trailing.push_back(0x00);
    CHECK(parseFails(trailing));

    CHECK(parseFails({0xF1, 0x01, 0x04, 'T', 0x00, 'x', 'y', 'z'}));
    CHECK(parseFails({0xD6, 0x00, 0x00}));
    CHECK(parseFails({0xD7, 0x00, 0x00}));
    CHECK(parseFails({0xD0, 0x01, 0x00, 'T'}));
    CHECK(parseFails({0xD2, 0x00, 0x01, 'x'}));
    CHECK(parseFails({0xD5, 0x01, 0x00, 'x'}));
    CHECK(parseFails({0xD1, 0x01, 0x08, 'T', 0x02, 'e', 'n', 'x'}));
    CHECK(parseFails({0xC2, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 'x'}));
    CHECK(parseFails({0xD9, 0x01, 0x01, 0x04, 'T', 0x01}));
    CHECK(parseFails({0xD1, 0x01, 0x02, 'T', 0x03, 'e'}));
    return true;
}

}  // namespace

int main()
{
    if (!testTextRecord() || !testTextRecordWithId() || !testUriPrefixTable() || !testMimeRecord() ||
        !testLongPayload() || !testMultipleRecords() || !testUnknownRecordPreservesRawData() ||
        !testMalformedMessages()) {
        return 1;
    }
    std::puts("NDEF parser tests passed");
    return 0;
}

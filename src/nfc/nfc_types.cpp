#include "nfc/nfc_types.hpp"

#include <iomanip>
#include <sstream>

namespace cap_nfc::nfc {
namespace {

bool sameRecord(const NdefRecord& left, const NdefRecord& right)
{
    return left.kind == right.kind && left.type == right.type && left.value == right.value &&
           left.payload == right.payload;
}

}  // namespace

const char* readerStateName(ReaderState state)
{
    switch (state) {
        case ReaderState::Stopped:
            return "Stopped";
        case ReaderState::Initializing:
            return "Initializing";
        case ReaderState::Scanning:
            return "Scanning";
        case ReaderState::Idle:
            return "Idle";
        case ReaderState::Error:
            return "Error";
        case ReaderState::Stopping:
            return "Stopping";
    }
    return "Unknown";
}

const char* tagTechnologyName(TagTechnology technology)
{
    switch (technology) {
        case TagTechnology::NfcA:
            return "NFC-A";
        case TagTechnology::NfcB:
            return "NFC-B";
        case TagTechnology::NfcF:
            return "NFC-F";
        case TagTechnology::NfcV:
            return "NFC-V";
        case TagTechnology::Unknown:
            return "Unknown";
    }
    return "Unknown";
}

const char* ndefRecordKindName(NdefRecordKind kind)
{
    switch (kind) {
        case NdefRecordKind::Text:
            return "TEXT";
        case NdefRecordKind::Uri:
            return "URI";
        case NdefRecordKind::Mime:
            return "MIME";
        case NdefRecordKind::Unknown:
            return "RAW";
    }
    return "RAW";
}

std::string bytesToHex(const std::vector<uint8_t>& bytes, const char* separator)
{
    if (bytes.empty()) {
        return "--";
    }

    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0 && separator) {
            stream << separator;
        }
        stream << std::setw(2) << static_cast<unsigned>(bytes[index]);
    }
    return stream.str();
}

bool sameTagIdentity(const TagSnapshot& left, const TagSnapshot& right)
{
    return left.technology == right.technology && left.uid == right.uid;
}

bool sameTagSnapshot(const TagSnapshot& left, const TagSnapshot& right)
{
    if (!sameTagIdentity(left, right) || left.atqa != right.atqa || left.sak != right.sak ||
        left.typeName != right.typeName || left.ndefSupported != right.ndefSupported ||
        left.ndefReadable != right.ndefReadable || left.ndefCapacity != right.ndefCapacity ||
        left.records.size() != right.records.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.records.size(); ++index) {
        if (!sameRecord(left.records[index], right.records[index])) {
            return false;
        }
    }
    return true;
}

}  // namespace cap_nfc::nfc

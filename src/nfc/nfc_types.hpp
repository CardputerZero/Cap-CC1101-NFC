#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cap_nfc::nfc {

enum class ReaderState {
    Stopped,
    Initializing,
    Scanning,
    Idle,
    Error,
    Stopping,
};

enum class TagTechnology {
    Unknown,
    NfcA,
    NfcB,
    NfcF,
    NfcV,
};

enum class NdefRecordKind {
    Text,
    Uri,
    Mime,
    Unknown,
};

struct NdefRecord {
    NdefRecordKind kind = NdefRecordKind::Unknown;
    std::string type;
    std::string value;
    std::vector<uint8_t> payload;
};

struct TagSnapshot {
    TagTechnology technology = TagTechnology::Unknown;
    std::vector<uint8_t> uid;
    std::vector<uint8_t> atqa;
    uint8_t sak = 0;
    std::string typeName;
    bool ndefSupported       = false;
    bool ndefReadable        = false;
    std::size_t ndefCapacity = 0;
    std::vector<NdefRecord> records;
};

struct TagSession {
    uint64_t sessionId   = 0;
    bool present         = false;
    uint64_t firstSeenMs = 0;
    uint64_t lastSeenMs  = 0;
    TagSnapshot snapshot;
};

struct ReaderInfo {
    std::string backendName;
    std::string chipName;
    std::string chipVersion;
    std::string transport;
    std::string irq;
    std::string power;
    std::string protocols;
    bool mock = false;
};

struct ReaderStatus {
    ReaderState state          = ReaderState::Stopped;
    bool ready                 = false;
    bool initializationFailed  = false;
    bool backendUnavailable    = false;
    std::size_t attempt        = 0;
    std::size_t failedAttempt  = 0;
    uint64_t scanCount         = 0;
    uint64_t droppedEventCount = 0;
    ReaderInfo info;
    std::string diagnostics{"Reader stopped"};
};

enum class DiscoveryPollKind {
    NoObservation,
    NoTag,
    Tag,
};

struct DiscoveryPollResult {
    DiscoveryPollKind kind = DiscoveryPollKind::NoObservation;
    std::optional<TagSnapshot> tag;
};

class NfcCancelled final : public std::exception {
public:
    const char* what() const noexcept override
    {
        return "NFC operation cancelled";
    }
};

class NfcBackendUnavailable final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class CancellationToken {
public:
    CancellationToken() = default;
    explicit CancellationToken(const std::atomic_bool& cancelled) : _cancelled(&cancelled)
    {
    }

    bool stopRequested() const noexcept
    {
        return _cancelled != nullptr && _cancelled->load(std::memory_order_acquire);
    }

    void throwIfCancellationRequested() const
    {
        if (stopRequested()) {
            throw NfcCancelled{};
        }
    }

    const std::atomic_bool* nativeFlag() const noexcept
    {
        return _cancelled;
    }

private:
    const std::atomic_bool* _cancelled = nullptr;
};

struct RetryCommand {};

struct SetScanningCommand {
    bool enabled = true;
};

struct ShutdownCommand {};

using NfcCommand = std::variant<RetryCommand, SetScanningCommand, ShutdownCommand>;

struct StateEvent {
    ReaderState state   = ReaderState::Stopped;
    std::size_t attempt = 0;
    std::string detail;
};

struct InitializedEvent {
    std::size_t attempt = 0;
    ReaderInfo info;
};

struct InitializationFailedEvent {
    std::size_t attempt = 0;
    std::string stage;
    std::string message;
    bool backendUnavailable = false;
};

struct TagPresentedEvent {
    uint64_t sessionId   = 0;
    uint64_t timestampMs = 0;
    TagSnapshot snapshot;
};

struct TagUpdatedEvent {
    uint64_t sessionId   = 0;
    uint64_t timestampMs = 0;
    TagSnapshot snapshot;
};

struct TagRemovedEvent {
    uint64_t sessionId   = 0;
    uint64_t timestampMs = 0;
    uint64_t lastSeenMs  = 0;
    TagSnapshot snapshot;
};

struct WorkerErrorEvent {
    std::string operation;
    std::string message;
    bool recoverable = true;
};

struct QueueOverflowEvent {
    std::size_t dropped = 0;
};

using NfcEvent = std::variant<StateEvent, InitializedEvent, InitializationFailedEvent, TagPresentedEvent,
                              TagUpdatedEvent, TagRemovedEvent, WorkerErrorEvent, QueueOverflowEvent>;

const char* readerStateName(ReaderState state);
const char* tagTechnologyName(TagTechnology technology);
const char* ndefRecordKindName(NdefRecordKind kind);
std::string bytesToHex(const std::vector<uint8_t>& bytes, const char* separator = " ");
bool sameTagIdentity(const TagSnapshot& left, const TagSnapshot& right);
bool sameTagSnapshot(const TagSnapshot& left, const TagSnapshot& right);

}  // namespace cap_nfc::nfc

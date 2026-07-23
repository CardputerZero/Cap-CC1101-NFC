#include "models/nfc_model.hpp"

#include "nfc/nfc_worker.hpp"

#include <spdlog/spdlog.h>

#include <exception>
#include <type_traits>
#include <utility>
#include <variant>

namespace cap_nfc {
namespace {

constexpr std::size_t kMaxEventsPerTick = 24;

const char* postResultText(nfc::NfcPostResult result)
{
    switch (result) {
        case nfc::NfcPostResult::Accepted:
            return "";
        case nfc::NfcPostResult::NotRunning:
            return "NFC worker is not running";
        case nfc::NfcPostResult::QueueFull:
            return "NFC command queue is full";
    }
    return "NFC command failed";
}

}  // namespace

NfcModel::NfcModel() : NfcModel(std::make_unique<nfc::NfcWorker>())
{
}

NfcModel::NfcModel(std::unique_ptr<nfc::NfcWorker> worker) : _worker(std::move(worker))
{
}

NfcModel::~NfcModel()
{
    stop();
}

void NfcModel::start()
{
    if (_started) {
        return;
    }

    _started = true;
    _tag_session.set(std::nullopt);

    nfc::ReaderStatus status;
    status.state       = nfc::ReaderState::Initializing;
    status.ready       = false;
    status.diagnostics = "Starting NFC reader";
    _reader_status.set(std::move(status));

    try {
        if (!_worker->start(true)) {
            auto failed                 = _reader_status.get();
            failed.state                = nfc::ReaderState::Error;
            failed.initializationFailed = false;
            failed.diagnostics          = "NFC worker is already running";
            _reader_status.set(std::move(failed));
        }
    } catch (const std::exception& exception) {
        auto failed                 = _reader_status.get();
        failed.state                = nfc::ReaderState::Error;
        failed.ready                = false;
        failed.initializationFailed = true;
        failed.failedAttempt        = 1;
        failed.diagnostics          = exception.what();
        _reader_status.set(std::move(failed));
    }
}

void NfcModel::stop()
{
    if (!_started) {
        return;
    }

    _worker->stop();
    auto session = _tag_session.get();
    if (session && session->present) {
        session->present = false;
        _tag_session.set(std::move(session));
    }
    auto status        = _reader_status.get();
    status.state       = nfc::ReaderState::Stopped;
    status.ready       = false;
    status.diagnostics = "NFC reader stopped";
    _reader_status.set(std::move(status));
    _started = false;
}

void NfcModel::tick(uint32_t nowMs)
{
    (void)nowMs;
    if (!_started) {
        return;
    }

    for (std::size_t index = 0; index < kMaxEventsPerTick; ++index) {
        nfc::NfcEvent event;
        if (!_worker->tryPopEvent(event)) {
            break;
        }

        std::visit(
            [this](auto&& value) {
                using Event = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Event, nfc::StateEvent>) {
                    auto status = _reader_status.get();
                    const bool preserveDetailedError =
                        value.state == nfc::ReaderState::Error &&
                        (status.initializationFailed || status.diagnostics.find(':') != std::string::npos);
                    status.state   = value.state;
                    status.attempt = value.attempt;
                    switch (value.state) {
                        case nfc::ReaderState::Initializing:
                            status.ready                = false;
                            status.initializationFailed = false;
                            status.backendUnavailable   = false;
                            status.diagnostics          = value.detail;
                            break;
                        case nfc::ReaderState::Scanning:
                        case nfc::ReaderState::Idle:
                            status.ready                = true;
                            status.initializationFailed = false;
                            status.backendUnavailable   = false;
                            status.diagnostics          = value.detail;
                            break;
                        case nfc::ReaderState::Error:
                            status.ready = false;
                            if (!preserveDetailedError) {
                                status.diagnostics = value.detail;
                            }
                            break;
                        case nfc::ReaderState::Stopping:
                        case nfc::ReaderState::Stopped:
                            status.ready = false;
                            if (!value.detail.empty()) {
                                status.diagnostics = value.detail;
                            }
                            break;
                    }
                    _reader_status.set(std::move(status));
                } else if constexpr (std::is_same_v<Event, nfc::InitializedEvent>) {
                    auto status                 = _reader_status.get();
                    status.state                = nfc::ReaderState::Scanning;
                    status.attempt              = value.attempt;
                    status.ready                = true;
                    status.initializationFailed = false;
                    status.backendUnavailable   = false;
                    status.info                 = std::move(value.info);
                    status.diagnostics          = status.info.mock ? "SDL mock reader ready" : "ST25R3916 ready";
                    _reader_status.set(std::move(status));
                } else if constexpr (std::is_same_v<Event, nfc::InitializationFailedEvent>) {
                    auto status                 = _reader_status.get();
                    status.state                = nfc::ReaderState::Error;
                    status.ready                = false;
                    status.initializationFailed = true;
                    status.backendUnavailable   = value.backendUnavailable;
                    status.attempt              = value.attempt;
                    status.failedAttempt        = value.attempt;
                    status.diagnostics          = value.stage + ": " + value.message;
                    _reader_status.set(std::move(status));
                } else if constexpr (std::is_same_v<Event, nfc::TagPresentedEvent>) {
                    nfc::TagSession session;
                    session.sessionId   = value.sessionId;
                    session.present     = true;
                    session.firstSeenMs = value.timestampMs;
                    session.lastSeenMs  = value.timestampMs;
                    session.snapshot    = std::move(value.snapshot);
                    _tag_session.set(std::move(session));

                    auto status = _reader_status.get();
                    ++status.scanCount;
                    status.diagnostics = "Tag detected";
                    _reader_status.set(std::move(status));
                } else if constexpr (std::is_same_v<Event, nfc::TagUpdatedEvent>) {
                    auto session = _tag_session.get();
                    if (session && session->sessionId == value.sessionId && session->present) {
                        session->lastSeenMs = value.timestampMs;
                        session->snapshot   = std::move(value.snapshot);
                        _tag_session.set(std::move(session));
                    }
                } else if constexpr (std::is_same_v<Event, nfc::TagRemovedEvent>) {
                    auto session = _tag_session.get();
                    if (session && session->sessionId == value.sessionId) {
                        session->present    = false;
                        session->lastSeenMs = value.lastSeenMs;
                        session->snapshot   = std::move(value.snapshot);
                        _tag_session.set(std::move(session));
                    }

                    auto status = _reader_status.get();
                    if (status.state != nfc::ReaderState::Error) {
                        status.diagnostics = "Tag removed";
                        _reader_status.set(std::move(status));
                    }
                } else if constexpr (std::is_same_v<Event, nfc::WorkerErrorEvent>) {
                    auto status        = _reader_status.get();
                    status.state       = nfc::ReaderState::Error;
                    status.ready       = false;
                    status.diagnostics = value.operation + ": " + value.message;
                    _reader_status.set(std::move(status));
                } else if constexpr (std::is_same_v<Event, nfc::QueueOverflowEvent>) {
                    auto status = _reader_status.get();
                    status.droppedEventCount += value.dropped;
                    if (status.state != nfc::ReaderState::Error) {
                        status.diagnostics = "Dropped " + std::to_string(value.dropped) + " NFC events";
                    }
                    _reader_status.set(std::move(status));
                }
            },
            std::move(event));
    }
}

bool NfcModel::retry()
{
    if (!_started) {
        return false;
    }

    auto status = _reader_status.get();
    if (status.backendUnavailable) {
        return false;
    }
    if (status.state != nfc::ReaderState::Error && status.state != nfc::ReaderState::Stopped) {
        return false;
    }

    status.state                = nfc::ReaderState::Initializing;
    status.ready                = false;
    status.initializationFailed = false;
    status.backendUnavailable   = false;
    status.diagnostics          = "Retrying NFC reader";
    _reader_status.set(std::move(status));

    auto session = _tag_session.get();
    if (session && session->present) {
        session->present = false;
        _tag_session.set(std::move(session));
    }

    try {
        if (!_worker->running()) {
            return _worker->start(true);
        }

        const nfc::NfcPostResult result = _worker->post(nfc::NfcCommand{nfc::RetryCommand{}});
        if (result == nfc::NfcPostResult::Accepted) {
            return true;
        }

        auto failed        = _reader_status.get();
        failed.state       = nfc::ReaderState::Error;
        failed.ready       = false;
        failed.diagnostics = postResultText(result);
        _reader_status.set(std::move(failed));
    } catch (const std::exception& exception) {
        auto failed        = _reader_status.get();
        failed.state       = nfc::ReaderState::Error;
        failed.ready       = false;
        failed.diagnostics = exception.what();
        _reader_status.set(std::move(failed));
    }
    return false;
}

}  // namespace cap_nfc

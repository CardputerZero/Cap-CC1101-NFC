#include "nfc/nfc_worker.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace cap_nfc::nfc {
namespace {

constexpr auto kDiscoverySlice = std::chrono::milliseconds(50);
#if defined(CAP_NFC_USE_LINUX_BACKEND) && CAP_NFC_USE_LINUX_BACKEND
// The current BSP exposes the NFC CS only as a userspace GPIO while the LCD
// shares SPI0. Leave time for deferred framebuffer flushes between NFC polls.
constexpr auto kMinimumDiscoveryCycle = std::chrono::milliseconds(80);
constexpr auto kPresentDiscoveryCycle = std::chrono::milliseconds(500);
#else
constexpr auto kMinimumDiscoveryCycle = std::chrono::milliseconds(20);
constexpr auto kPresentDiscoveryCycle = kMinimumDiscoveryCycle;
#endif
constexpr std::size_t kPresentConfirmations = 2;
constexpr std::size_t kRemoveConfirmations  = 3;
constexpr uint64_t kRemoveDebounceMs        = 160;

uint64_t monotonicMilliseconds()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::string exceptionMessage(const std::exception& exception)
{
    const char* message = exception.what();
    return message && message[0] != '\0' ? message : "unknown NFC error";
}

bool isDroppableEvent(const NfcEvent& event)
{
    return std::holds_alternative<TagUpdatedEvent>(event) || std::holds_alternative<QueueOverflowEvent>(event);
}

class InitializationGuard {
public:
    explicit InitializationGuard(std::atomic_bool& active) : _active(active)
    {
        _active.store(true, std::memory_order_release);
    }

    ~InitializationGuard()
    {
        release();
    }

    InitializationGuard(const InitializationGuard&)            = delete;
    InitializationGuard& operator=(const InitializationGuard&) = delete;

    void release()
    {
        if (_owns_flag) {
            _active.store(false, std::memory_order_release);
            _owns_flag = false;
        }
    }

private:
    std::atomic_bool& _active;
    bool _owns_flag = true;
};

}  // namespace

NfcWorker::NfcWorker() : NfcWorker(makeDefaultNfcBackend())
{
}

NfcWorker::NfcWorker(std::unique_ptr<NfcBackend> backend) : _backend(std::move(backend))
{
    if (!_backend) {
        throw std::invalid_argument("NfcWorker requires a backend");
    }
}

NfcWorker::~NfcWorker()
{
    stop();
}

bool NfcWorker::start(bool scanOnStart)
{
    std::lock_guard<std::mutex> lifecycleLock(_lifecycle_mutex);
    if (_running.load(std::memory_order_acquire)) {
        return false;
    }
    if (_thread.joinable()) {
        _thread.join();
    }

    {
        std::lock_guard<std::mutex> commandLock(_command_mutex);
        _commands.clear();
        _stop_requested.store(false, std::memory_order_release);
        _running.store(true, std::memory_order_release);
    }
    {
        std::lock_guard<std::mutex> eventLock(_event_mutex);
        _events.clear();
        _pending_dropped_events = 0;
    }

    _scan_on_start   = scanOnStart;
    _attempt         = 0;
    _next_session_id = 1;
    clearPresence(false);
    try {
        _thread = std::thread(&NfcWorker::run, this);
    } catch (...) {
        std::lock_guard<std::mutex> commandLock(_command_mutex);
        _running.store(false, std::memory_order_release);
        throw;
    }
    return true;
}

void NfcWorker::stop()
{
    std::lock_guard<std::mutex> lifecycleLock(_lifecycle_mutex);
    {
        std::lock_guard<std::mutex> commandLock(_command_mutex);
        _stop_requested.store(true, std::memory_order_release);
    }
    _command_cv.notify_all();
    if (_thread.joinable()) {
        const auto startedAt = std::chrono::steady_clock::now();
        spdlog::info("NFC worker: stop requested; joining hardware thread");
        _thread.join();
        const auto elapsedMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt).count();
        spdlog::info("NFC worker: hardware thread joined after {} ms", elapsedMs);
    }
    _running.store(false, std::memory_order_release);
}

NfcPostResult NfcWorker::post(NfcCommand command)
{
    std::unique_lock<std::mutex> lock(_command_mutex);
    if (!_running.load(std::memory_order_acquire) || _stop_requested.load(std::memory_order_acquire)) {
        return NfcPostResult::NotRunning;
    }
    if (std::holds_alternative<ShutdownCommand>(command)) {
        _stop_requested.store(true, std::memory_order_release);
        lock.unlock();
        _command_cv.notify_all();
        return NfcPostResult::Accepted;
    }

    if (std::holds_alternative<RetryCommand>(command)) {
        if (_initialization_in_progress.load(std::memory_order_acquire)) {
            return NfcPostResult::Accepted;
        }
        const auto pendingRetry = std::find_if(_commands.begin(), _commands.end(), [](const NfcCommand& queued) {
            return std::holds_alternative<RetryCommand>(queued);
        });
        if (pendingRetry != _commands.end()) {
            return NfcPostResult::Accepted;
        }
    }

    if (_commands.size() >= kCommandQueueCapacity) {
        return NfcPostResult::QueueFull;
    }
    _commands.push_back(std::move(command));
    lock.unlock();
    _command_cv.notify_one();
    return NfcPostResult::Accepted;
}

bool NfcWorker::tryPopEvent(NfcEvent& event)
{
    std::lock_guard<std::mutex> lock(_event_mutex);
    if (_pending_dropped_events != 0) {
        event                   = QueueOverflowEvent{_pending_dropped_events};
        _pending_dropped_events = 0;
        return true;
    }
    if (_events.empty()) {
        return false;
    }

    event = std::move(_events.front());
    _events.pop_front();
    return true;
}

void NfcWorker::run()
{
    const CancellationToken cancellation(_stop_requested);
    bool initialized   = false;
    bool scanRequested = _scan_on_start;

    try {
        initialized = initializeBackend(scanRequested, cancellation);
        while (!_stop_requested.load(std::memory_order_acquire)) {
            NfcCommand command;
            if (tryPopCommand(command)) {
                handleCommand(std::move(command), initialized, scanRequested, cancellation);
                continue;
            }

            if (!initialized) {
                std::unique_lock<std::mutex> lock(_command_mutex);
                _command_cv.wait(
                    lock, [this]() { return _stop_requested.load(std::memory_order_acquire) || !_commands.empty(); });
                continue;
            }

            if (!scanRequested) {
                std::unique_lock<std::mutex> lock(_command_mutex);
                _command_cv.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                    return _stop_requested.load(std::memory_order_acquire) || !_commands.empty();
                });
                continue;
            }

            try {
                const auto pollStartedAt = std::chrono::steady_clock::now();
                handleDiscoveryResult(_backend->pollDiscovery(kDiscoverySlice, cancellation));
                const auto pollElapsed = std::chrono::steady_clock::now() - pollStartedAt;
                const auto discoveryCycle =
                    _present_tag && _explicit_no_tag_count == 0 ? kPresentDiscoveryCycle : kMinimumDiscoveryCycle;
                if (pollElapsed < discoveryCycle) {
                    std::unique_lock<std::mutex> lock(_command_mutex);
                    _command_cv.wait_for(lock, discoveryCycle - pollElapsed, [this]() {
                        return _stop_requested.load(std::memory_order_acquire) || !_commands.empty();
                    });
                }
            } catch (const NfcCancelled&) {
                throw;
            } catch (const std::exception& exception) {
                const std::string message = exceptionMessage(exception);
                spdlog::error("NFC worker: discovery failed: {}", message);
                pushEvent(WorkerErrorEvent{"discovery", message, true});
                clearPresence(true);
                _backend->closeImmediately();
                initialized = false;
                pushState(ReaderState::Error, "Discovery failed; retry required");
            }
        }
    } catch (const NfcCancelled&) {
    } catch (const std::exception& exception) {
        if (!_stop_requested.load(std::memory_order_acquire)) {
            const std::string message = exceptionMessage(exception);
            spdlog::error("NFC worker stopped unexpectedly: {}", message);
            pushEvent(WorkerErrorEvent{"worker", message, false});
            pushState(ReaderState::Error, "NFC worker stopped unexpectedly");
        }
    }

    {
        std::lock_guard<std::mutex> commandLock(_command_mutex);
        _running.store(false, std::memory_order_release);
    }
    spdlog::info("NFC worker: hardware loop stopped; closing backend");
    pushState(ReaderState::Stopping, "Stopping NFC reader");
    clearPresence(true);
    if (_stop_requested.load(std::memory_order_acquire)) {
        _backend->closeImmediately();
    } else {
        _backend->close();
    }
    pushState(ReaderState::Stopped, "NFC reader stopped");
    spdlog::info("NFC worker: backend closed");
}

bool NfcWorker::initializeBackend(bool scanRequested, const CancellationToken& cancellation)
{
    InitializationGuard initializationGuard(_initialization_in_progress);
    const std::size_t attempt = ++_attempt;
    const auto startedAt      = std::chrono::steady_clock::now();
    const auto elapsedMs      = [&startedAt]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt)
            .count();
    };

    spdlog::info("NFC reader: initialization attempt {} (scan_on_start={})", attempt, scanRequested);
    pushState(ReaderState::Initializing, "Initializing ST25R3916");
    clearPresence(true);
    _backend->close();

    std::string stage = "open";
    try {
        ReaderInfo info = _backend->open(cancellation);
        if (scanRequested) {
            stage = "start discovery";
            _backend->startDiscovery(cancellation);
        }

        spdlog::info("NFC reader: backend ready after {} ms (backend={}, chip={}, transport={}, irq={}, power={})",
                     elapsedMs(), info.backendName, info.chipName, info.transport, info.irq, info.power);
        initializationGuard.release();
        pushEvent(InitializedEvent{attempt, std::move(info)});
        pushState(scanRequested ? ReaderState::Scanning : ReaderState::Idle,
                  scanRequested ? "Scanning for NFC tags" : "Reader idle");
        return true;
    } catch (const NfcCancelled&) {
        spdlog::debug("NFC reader: initialization attempt {} cancelled after {} ms", attempt, elapsedMs());
        throw;
    } catch (const NfcBackendUnavailable& exception) {
        const std::string message = exceptionMessage(exception);
        spdlog::error("NFC reader: initialization attempt {} has no hardware backend: {}", attempt, message);
        _backend->closeImmediately();
        initializationGuard.release();
        pushEvent(InitializationFailedEvent{attempt, stage, message, true});
        pushState(ReaderState::Error, "Hardware backend unavailable");
        return false;
    } catch (const std::exception& exception) {
        const std::string message = exceptionMessage(exception);
        spdlog::error("NFC reader: initialization attempt {} failed at {} after {} ms: {}", attempt, stage, elapsedMs(),
                      message);
        _backend->closeImmediately();
        initializationGuard.release();
        pushEvent(InitializationFailedEvent{attempt, stage, message, false});
        pushState(ReaderState::Error, "Initialization failed; retry required");
        return false;
    }
}

bool NfcWorker::tryPopCommand(NfcCommand& command)
{
    std::lock_guard<std::mutex> lock(_command_mutex);
    if (_commands.empty()) {
        return false;
    }
    command = std::move(_commands.front());
    _commands.pop_front();
    return true;
}

void NfcWorker::handleCommand(NfcCommand command, bool& initialized, bool& scanRequested,
                              const CancellationToken& cancellation)
{
    if (std::holds_alternative<RetryCommand>(command)) {
        initialized = initializeBackend(scanRequested, cancellation);
        return;
    }

    if (auto* setScanning = std::get_if<SetScanningCommand>(&command)) {
        scanRequested = setScanning->enabled;
        if (!initialized) {
            return;
        }

        try {
            if (scanRequested) {
                _backend->startDiscovery(cancellation);
                pushState(ReaderState::Scanning, "Scanning for NFC tags");
            } else {
                _backend->stopDiscovery();
                clearPresence(true);
                pushState(ReaderState::Idle, "Reader idle");
            }
        } catch (const NfcCancelled&) {
            throw;
        } catch (const std::exception& exception) {
            const std::string message = exceptionMessage(exception);
            pushEvent(WorkerErrorEvent{"set scanning", message, true});
            clearPresence(true);
            _backend->closeImmediately();
            initialized = false;
            pushState(ReaderState::Error, "Scan mode change failed; retry required");
        }
    }
}

void NfcWorker::handleDiscoveryResult(DiscoveryPollResult result)
{
    if (result.kind == DiscoveryPollKind::NoObservation) {
        _explicit_no_tag_count    = 0;
        _first_explicit_no_tag_ms = 0;
        return;
    }

    if (result.kind == DiscoveryPollKind::NoTag) {
        _candidate_tag.reset();
        _candidate_confirmations = 0;
        if (_present_tag) {
            const uint64_t nowMs = monotonicMilliseconds();
            if (_explicit_no_tag_count == 0) {
                _first_explicit_no_tag_ms = nowMs;
            }
            ++_explicit_no_tag_count;
            if (_explicit_no_tag_count >= kRemoveConfirmations &&
                nowMs - _first_explicit_no_tag_ms >= kRemoveDebounceMs) {
                clearPresence(true);
            }
        }
        return;
    }

    if (result.kind != DiscoveryPollKind::Tag || !result.tag) {
        if (result.kind == DiscoveryPollKind::Tag) {
            spdlog::warn("NFC reader: backend returned Tag without a snapshot; ignoring the observation");
        }
        _explicit_no_tag_count    = 0;
        _first_explicit_no_tag_ms = 0;
        return;
    }

    TagSnapshot observed      = std::move(*result.tag);
    _explicit_no_tag_count    = 0;
    _first_explicit_no_tag_ms = 0;
    const uint64_t nowMs      = monotonicMilliseconds();

    if (_present_tag) {
        if (sameTagIdentity(*_present_tag, observed)) {
            _candidate_tag.reset();
            _candidate_confirmations     = 0;
            _last_present_observation_ms = nowMs;
            if (!sameTagSnapshot(*_present_tag, observed)) {
                _present_tag = observed;
                spdlog::info("NFC reader: tag updated (session={}, tech={}, uid={})", _active_session_id,
                             tagTechnologyName(observed.technology), bytesToHex(observed.uid));
                pushEvent(TagUpdatedEvent{_active_session_id, nowMs, std::move(observed)});
            }
            return;
        }

        if (_candidate_tag && sameTagIdentity(*_candidate_tag, observed)) {
            _candidate_tag = observed;
            ++_candidate_confirmations;
        } else {
            _candidate_tag           = observed;
            _candidate_confirmations = 1;
        }
        if (_candidate_confirmations < kPresentConfirmations) {
            return;
        }

        TagSnapshot replacement = *_candidate_tag;
        clearPresence(true);
        _present_tag                 = std::move(replacement);
        _active_session_id           = _next_session_id++;
        _last_present_observation_ms = nowMs;
        spdlog::info("NFC reader: tag presented (session={}, tech={}, uid={}, type={})", _active_session_id,
                     tagTechnologyName(_present_tag->technology), bytesToHex(_present_tag->uid),
                     _present_tag->typeName);
        pushEvent(TagPresentedEvent{_active_session_id, nowMs, *_present_tag});
        return;
    }

    if (_candidate_tag && sameTagIdentity(*_candidate_tag, observed)) {
        _candidate_tag = observed;
        ++_candidate_confirmations;
    } else {
        _candidate_tag           = observed;
        _candidate_confirmations = 1;
    }

    if (_candidate_confirmations < kPresentConfirmations) {
        return;
    }

    _present_tag                 = *_candidate_tag;
    _active_session_id           = _next_session_id++;
    _last_present_observation_ms = nowMs;
    spdlog::info("NFC reader: tag presented (session={}, tech={}, uid={}, type={})", _active_session_id,
                 tagTechnologyName(_present_tag->technology), bytesToHex(_present_tag->uid), _present_tag->typeName);
    pushEvent(TagPresentedEvent{_active_session_id, nowMs, *_present_tag});
    _candidate_tag.reset();
    _candidate_confirmations = 0;
}

void NfcWorker::clearPresence(bool emitRemoval)
{
    if (emitRemoval && _present_tag) {
        const uint64_t removedAtMs = monotonicMilliseconds();
        const uint64_t lastSeenMs  = _last_present_observation_ms != 0 ? _last_present_observation_ms : removedAtMs;
        spdlog::info("NFC reader: tag removed (session={}, tech={}, uid={})", _active_session_id,
                     tagTechnologyName(_present_tag->technology), bytesToHex(_present_tag->uid));
        pushEvent(TagRemovedEvent{_active_session_id, removedAtMs, lastSeenMs, *_present_tag});
    }
    _present_tag.reset();
    _candidate_tag.reset();
    _candidate_confirmations     = 0;
    _explicit_no_tag_count       = 0;
    _active_session_id           = 0;
    _first_explicit_no_tag_ms    = 0;
    _last_present_observation_ms = 0;
}

void NfcWorker::pushEvent(NfcEvent event)
{
    std::lock_guard<std::mutex> lock(_event_mutex);
    if (_events.size() >= kEventQueueCapacity) {
        if (isDroppableEvent(event)) {
            ++_pending_dropped_events;
            return;
        }
        const auto droppable = std::find_if(_events.begin(), _events.end(), isDroppableEvent);
        if (droppable != _events.end()) {
            _events.erase(droppable);
        } else {
            _events.pop_front();
        }
        ++_pending_dropped_events;
    }
    _events.push_back(std::move(event));
}

void NfcWorker::pushState(ReaderState state, std::string detail)
{
    pushEvent(StateEvent{state, _attempt, std::move(detail)});
}

}  // namespace cap_nfc::nfc

#pragma once

#include "nfc/nfc_backend.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace cap_nfc::nfc {

enum class NfcPostResult {
    Accepted,
    NotRunning,
    QueueFull,
};

class NfcWorker {
public:
    static constexpr std::size_t kCommandQueueCapacity = 8;
    static constexpr std::size_t kEventQueueCapacity   = 48;

    NfcWorker();
    explicit NfcWorker(std::unique_ptr<NfcBackend> backend);
    ~NfcWorker();

    NfcWorker(const NfcWorker&)            = delete;
    NfcWorker& operator=(const NfcWorker&) = delete;

    bool start(bool scanOnStart = true);
    void stop();
    NfcPostResult post(NfcCommand command);
    bool tryPopEvent(NfcEvent& event);

    bool running() const noexcept
    {
        return _running.load(std::memory_order_acquire);
    }

private:
    std::unique_ptr<NfcBackend> _backend;
    std::thread _thread;
    mutable std::mutex _lifecycle_mutex;
    std::atomic_bool _running{false};
    std::atomic_bool _stop_requested{false};
    std::atomic_bool _initialization_in_progress{false};
    bool _scan_on_start  = true;
    std::size_t _attempt = 0;

    mutable std::mutex _command_mutex;
    std::condition_variable _command_cv;
    std::deque<NfcCommand> _commands;

    mutable std::mutex _event_mutex;
    std::deque<NfcEvent> _events;
    std::size_t _pending_dropped_events = 0;

    std::optional<TagSnapshot> _present_tag;
    std::optional<TagSnapshot> _candidate_tag;
    std::size_t _candidate_confirmations  = 0;
    std::size_t _explicit_no_tag_count    = 0;
    uint64_t _active_session_id           = 0;
    uint64_t _next_session_id             = 1;
    uint64_t _first_explicit_no_tag_ms    = 0;
    uint64_t _last_present_observation_ms = 0;

    void run();
    bool initializeBackend(bool scanRequested, const CancellationToken& cancellation);
    bool tryPopCommand(NfcCommand& command);
    void handleCommand(NfcCommand command, bool& initialized, bool& scanRequested,
                       const CancellationToken& cancellation);
    void handleDiscoveryResult(DiscoveryPollResult result);
    void clearPresence(bool emitRemoval);
    void pushEvent(NfcEvent event);
    void pushState(ReaderState state, std::string detail = {});
};

}  // namespace cap_nfc::nfc

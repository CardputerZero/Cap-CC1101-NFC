#include "models/nfc_model.hpp"
#include "nfc/nfc_backend.hpp"
#include "nfc/nfc_worker.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace cap_nfc::nfc;
using namespace std::chrono_literals;
using cap_nfc::NfcModel;

#define CHECK(condition)                                                                         \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                                        \
        }                                                                                        \
    } while (false)

TagSnapshot makeTag(uint8_t suffix)
{
    TagSnapshot tag;
    tag.technology    = TagTechnology::NfcA;
    tag.uid           = {0x04, 0xA1, 0x6B, 0x7C, 0x92, 0x64, suffix};
    tag.atqa          = {0x00, 0x44};
    tag.sak           = 0x00;
    tag.typeName      = "Type 2";
    tag.ndefSupported = true;
    tag.ndefReadable  = true;
    tag.ndefCapacity  = 144;
    return tag;
}

DiscoveryPollResult tagResult(const TagSnapshot& tag)
{
    return {DiscoveryPollKind::Tag, tag};
}

class ScriptBackend final : public NfcBackend {
public:
    ScriptBackend(std::vector<DiscoveryPollResult> script, std::size_t failedOpenCount = 0,
                  std::shared_ptr<std::atomic_size_t> openAttempts = {}, std::chrono::milliseconds openDelay = 0ms,
                  std::shared_ptr<std::atomic_bool> immediateCloseObserved = {})
        : _script(std::move(script)),
          _failed_open_count(failedOpenCount),
          _open_attempts_observer(std::move(openAttempts)),
          _open_delay(openDelay),
          _immediate_close_observer(std::move(immediateCloseObserved))
    {
    }

    ReaderInfo open(const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        ++_open_attempts;
        if (_open_attempts_observer) {
            _open_attempts_observer->store(_open_attempts, std::memory_order_release);
        }
        const auto deadline = std::chrono::steady_clock::now() + _open_delay;
        while (std::chrono::steady_clock::now() < deadline) {
            cancellation.throwIfCancellationRequested();
            std::this_thread::sleep_for(1ms);
        }
        if (_open_attempts <= _failed_open_count) {
            throw std::runtime_error("scripted open failure");
        }
        _open = true;
        return ReaderInfo{"Script backend", "ST25R3916", "test", "test-spi", "test-irq", "test-power", "NFC-A", true};
    }

    void close() noexcept override
    {
        _open     = false;
        _scanning = false;
    }

    void closeImmediately() noexcept override
    {
        if (_immediate_close_observer) {
            _immediate_close_observer->store(true, std::memory_order_release);
        }
        close();
    }

    void startDiscovery(const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        if (!_open) {
            throw std::runtime_error("reader is closed");
        }
        _scanning = true;
    }

    void stopDiscovery() noexcept override
    {
        _scanning = false;
    }

    DiscoveryPollResult pollDiscovery(std::chrono::milliseconds timeout, const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        if (!_open || !_scanning) {
            throw std::runtime_error("reader is not scanning");
        }

        if (_script_index < _script.size()) {
            return _script[_script_index++];
        }

        const auto deadline = std::chrono::steady_clock::now() + std::min(timeout, 5ms);
        while (std::chrono::steady_clock::now() < deadline) {
            cancellation.throwIfCancellationRequested();
            std::this_thread::sleep_for(1ms);
        }
        return {};
    }

private:
    std::vector<DiscoveryPollResult> _script;
    std::size_t _script_index      = 0;
    std::size_t _failed_open_count = 0;
    std::size_t _open_attempts     = 0;
    std::shared_ptr<std::atomic_size_t> _open_attempts_observer;
    std::chrono::milliseconds _open_delay{0};
    std::shared_ptr<std::atomic_bool> _immediate_close_observer;
    bool _open     = false;
    bool _scanning = false;
};

template <typename Predicate>
bool collectUntil(NfcWorker& worker, std::vector<NfcEvent>& events, Predicate predicate,
                  std::chrono::milliseconds timeout = 1000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        NfcEvent event;
        while (worker.tryPopEvent(event)) {
            events.push_back(std::move(event));
            if (predicate(events.back())) {
                return true;
            }
        }
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

bool testPresentAndRemoveDebounce()
{
    const TagSnapshot tag                   = makeTag(0x80);
    std::vector<DiscoveryPollResult> script = {
        tagResult(tag),
        tagResult(tag),
        {DiscoveryPollKind::NoObservation, std::nullopt},
    };
    for (int index = 0; index < 12; ++index) {
        script.push_back({DiscoveryPollKind::NoTag, std::nullopt});
    }
    NfcWorker worker(std::make_unique<ScriptBackend>(std::move(script)));
    CHECK(worker.start());

    std::vector<NfcEvent> events;
    CHECK(collectUntil(worker, events,
                       [](const NfcEvent& event) { return std::holds_alternative<TagRemovedEvent>(event); }));
    worker.stop();

    const auto presented = std::find_if(events.begin(), events.end(), [](const NfcEvent& event) {
        return std::holds_alternative<TagPresentedEvent>(event);
    });
    const auto removed   = std::find_if(events.begin(), events.end(), [](const NfcEvent& event) {
        return std::holds_alternative<TagRemovedEvent>(event);
    });
    CHECK(presented != events.end());
    CHECK(removed != events.end());
    CHECK(presented < removed);
    const auto& presentedEvent = std::get<TagPresentedEvent>(*presented);
    const auto& removedEvent   = std::get<TagRemovedEvent>(*removed);
    CHECK(presentedEvent.sessionId == removedEvent.sessionId);
    CHECK(removedEvent.lastSeenMs == presentedEvent.timestampMs);
    CHECK(removedEvent.timestampMs >= removedEvent.lastSeenMs);
    return true;
}

bool testNoObservationDoesNotRemove()
{
    const TagSnapshot tag   = makeTag(0x81);
    const TagSnapshot noise = makeTag(0x91);
    NfcWorker worker(std::make_unique<ScriptBackend>(
        std::vector<DiscoveryPollResult>{tagResult(tag),
                                         tagResult(tag),
                                         tagResult(noise),
                                         {DiscoveryPollKind::NoTag, std::nullopt},
                                         {DiscoveryPollKind::Tag, std::nullopt},
                                         {DiscoveryPollKind::NoObservation, std::nullopt}}));
    CHECK(worker.start());

    std::vector<NfcEvent> events;
    CHECK(collectUntil(worker, events,
                       [](const NfcEvent& event) { return std::holds_alternative<TagPresentedEvent>(event); }));
    std::this_thread::sleep_for(260ms);
    NfcEvent event;
    while (worker.tryPopEvent(event)) {
        events.push_back(std::move(event));
    }

    CHECK(std::none_of(events.begin(), events.end(),
                       [](const NfcEvent& queued) { return std::holds_alternative<TagRemovedEvent>(queued); }));
    worker.stop();
    return true;
}

bool testTagReplacementOrdering()
{
    const TagSnapshot first  = makeTag(0x82);
    const TagSnapshot second = makeTag(0x83);
    NfcWorker worker(std::make_unique<ScriptBackend>(
        std::vector<DiscoveryPollResult>{tagResult(first), tagResult(first), tagResult(second), tagResult(second)}));
    CHECK(worker.start());

    std::vector<NfcEvent> events;
    std::size_t presentationCount = 0;
    CHECK(collectUntil(worker, events, [&presentationCount](const NfcEvent& event) {
        if (std::holds_alternative<TagPresentedEvent>(event)) {
            ++presentationCount;
        }
        return presentationCount == 2;
    }));
    worker.stop();

    std::vector<char> order;
    for (const auto& event : events) {
        if (std::holds_alternative<TagPresentedEvent>(event)) {
            order.push_back('P');
        } else if (std::holds_alternative<TagRemovedEvent>(event)) {
            order.push_back('R');
        }
    }
    CHECK(order == std::vector<char>({'P', 'R', 'P'}));
    return true;
}

bool testRetryAfterInitializationFailure()
{
    NfcWorker worker(std::make_unique<ScriptBackend>(std::vector<DiscoveryPollResult>{}, 1));
    CHECK(worker.start());

    std::vector<NfcEvent> events;
    CHECK(collectUntil(worker, events,
                       [](const NfcEvent& event) { return std::holds_alternative<InitializationFailedEvent>(event); }));
    CHECK(worker.post(NfcCommand{RetryCommand{}}) == NfcPostResult::Accepted);
    CHECK(collectUntil(worker, events,
                       [](const NfcEvent& event) { return std::holds_alternative<InitializedEvent>(event); }));
    worker.stop();

    const auto failed      = std::find_if(events.begin(), events.end(), [](const NfcEvent& event) {
        return std::holds_alternative<InitializationFailedEvent>(event);
    });
    const auto initialized = std::find_if(events.begin(), events.end(), [](const NfcEvent& event) {
        return std::holds_alternative<InitializedEvent>(event);
    });
    CHECK(failed != events.end());
    CHECK(initialized != events.end());
    CHECK(std::get<InitializationFailedEvent>(*failed).attempt == 1);
    CHECK(std::get<InitializedEvent>(*initialized).attempt == 2);
    return true;
}

bool testRetryCoalescedWhileInitializing()
{
    auto openAttempts = std::make_shared<std::atomic_size_t>(0);
    NfcWorker worker(std::make_unique<ScriptBackend>(std::vector<DiscoveryPollResult>{}, 1, openAttempts, 120ms));
    CHECK(worker.start());

    std::vector<NfcEvent> events;
    CHECK(collectUntil(worker, events,
                       [](const NfcEvent& event) { return std::holds_alternative<InitializationFailedEvent>(event); }));
    CHECK(worker.post(NfcCommand{RetryCommand{}}) == NfcPostResult::Accepted);

    const auto retryStartedDeadline = std::chrono::steady_clock::now() + 500ms;
    while (openAttempts->load(std::memory_order_acquire) < 2 &&
           std::chrono::steady_clock::now() < retryStartedDeadline) {
        std::this_thread::sleep_for(1ms);
    }
    CHECK(openAttempts->load(std::memory_order_acquire) == 2);
    CHECK(worker.post(NfcCommand{RetryCommand{}}) == NfcPostResult::Accepted);
    CHECK(worker.post(NfcCommand{RetryCommand{}}) == NfcPostResult::Accepted);

    CHECK(collectUntil(worker, events,
                       [](const NfcEvent& event) { return std::holds_alternative<InitializedEvent>(event); }));
    std::this_thread::sleep_for(160ms);
    worker.stop();

    CHECK(openAttempts->load(std::memory_order_acquire) == 2);
    return true;
}

bool testStopUsesImmediateBackendClose()
{
    auto immediateCloseObserved = std::make_shared<std::atomic_bool>(false);
    NfcWorker worker(
        std::make_unique<ScriptBackend>(std::vector<DiscoveryPollResult>{}, 0, nullptr, 0ms, immediateCloseObserved));
    CHECK(worker.start());

    std::vector<NfcEvent> events;
    CHECK(collectUntil(worker, events,
                       [](const NfcEvent& event) { return std::holds_alternative<InitializedEvent>(event); }));
    worker.stop();

    CHECK(immediateCloseObserved->load(std::memory_order_acquire));
    return true;
}

bool testModelPreservesLastSeenWhenTagIsRemoved()
{
    const TagSnapshot tag                   = makeTag(0x84);
    std::vector<DiscoveryPollResult> script = {tagResult(tag), tagResult(tag)};
    for (int index = 0; index < 12; ++index) {
        script.push_back({DiscoveryPollKind::NoTag, std::nullopt});
    }

    auto worker = std::make_unique<NfcWorker>(std::make_unique<ScriptBackend>(std::move(script)));
    NfcModel model(std::move(worker));
    model.start();

    bool observedRemoval = false;
    const auto deadline  = std::chrono::steady_clock::now() + 1500ms;
    while (std::chrono::steady_clock::now() < deadline) {
        model.tick(0);
        const auto& session = model.tagSession().get();
        if (session && !session->present) {
            CHECK(session->firstSeenMs != 0);
            CHECK(session->lastSeenMs == session->firstSeenMs);
            observedRemoval = true;
            break;
        }
        std::this_thread::sleep_for(2ms);
    }
    model.stop();

    CHECK(observedRemoval);
    return true;
}

}  // namespace

int main()
{
    if (!testPresentAndRemoveDebounce() || !testNoObservationDoesNotRemove() || !testTagReplacementOrdering() ||
        !testRetryAfterInitializationFailure() || !testRetryCoalescedWhileInitializing() ||
        !testStopUsesImmediateBackendClose() || !testModelPreservesLastSeenWhenTagIsRemoved()) {
        return 1;
    }
    std::puts("NFC worker tests passed");
    return 0;
}

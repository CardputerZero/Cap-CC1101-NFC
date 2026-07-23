#pragma once

#include "nfc/nfc_types.hpp"

#include <memory>
#include <optional>
#include <tools/observable/single_observable.hpp>

namespace cap_nfc::nfc {
class NfcWorker;
}

namespace cap_nfc {

class NfcModel {
public:
    NfcModel();
    explicit NfcModel(std::unique_ptr<nfc::NfcWorker> worker);
    ~NfcModel();

    NfcModel(const NfcModel&)            = delete;
    NfcModel& operator=(const NfcModel&) = delete;

    void start();
    void stop();
    void tick(uint32_t nowMs);
    bool retry();

    smooth_ui_toolkit::SingleObservable<nfc::ReaderStatus>& readerStatus()
    {
        return _reader_status;
    }

    smooth_ui_toolkit::SingleObservable<std::optional<nfc::TagSession>>& tagSession()
    {
        return _tag_session;
    }

private:
    std::unique_ptr<nfc::NfcWorker> _worker;
    smooth_ui_toolkit::SingleObservable<nfc::ReaderStatus> _reader_status{nfc::ReaderStatus{}};
    smooth_ui_toolkit::SingleObservable<std::optional<nfc::TagSession>> _tag_session{std::nullopt};
    bool _started = false;
};

}  // namespace cap_nfc

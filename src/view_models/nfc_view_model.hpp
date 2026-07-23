#pragma once

#include "models/nfc_model.hpp"
#include "view_models/view_model.hpp"

#include <optional>
#include <tools/observable/single_observable.hpp>

namespace cap_nfc {

class NfcViewModel : public ViewModel {
public:
    NfcViewModel(NfcRouter& router, NfcModel& model);

    PageId pageId() const override
    {
        return PageId::Nfc;
    }

    void onEnter() override;
    void onExit() override;
    void onKey(uint32_t key) override;
    void tick(uint32_t nowMs) override;

    smooth_ui_toolkit::SingleObservable<nfc::ReaderStatus>& readerStatus()
    {
        return _model.readerStatus();
    }

    smooth_ui_toolkit::SingleObservable<std::optional<nfc::TagSession>>& tagSession()
    {
        return _model.tagSession();
    }

    smooth_ui_toolkit::SingleObservable<NfcSection>& section()
    {
        return _section;
    }

    smooth_ui_toolkit::SingleObservable<ScrollRequest>& scrollRequest()
    {
        return _scroll_request;
    }

    smooth_ui_toolkit::SingleObservable<bool>& initializationDialogActive()
    {
        return _initialization_dialog_active;
    }

    bool modalActive() const
    {
        return _initialization_dialog_modal_active;
    }

    bool atRoot() const
    {
        return _section.get() == NfcSection::Scan;
    }

    void dismissInitializationDialog();
    void notifyInitializationDialogHidden();
    void retryReader();

private:
    NfcModel& _model;
    smooth_ui_toolkit::SingleObservable<NfcSection> _section{NfcSection::Scan};
    smooth_ui_toolkit::SingleObservable<ScrollRequest> _scroll_request{ScrollRequest{}};
    smooth_ui_toolkit::SingleObservable<bool> _initialization_dialog_active{false};
    uint32_t _scroll_serial                  = 0;
    std::size_t _last_failed_attempt         = 0;
    bool _initialization_dialog_modal_active = false;

    bool canRetryReader() const;
    void showInitializationDialog();
    void setSection(NfcSection section);
    void selectPreviousSection();
    void selectNextSection();
    void requestScroll(int32_t amount);
};

}  // namespace cap_nfc

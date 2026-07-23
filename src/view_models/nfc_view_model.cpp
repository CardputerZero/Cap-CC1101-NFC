#include "view_models/nfc_view_model.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace cap_nfc {
namespace {

bool isPreviousKey(uint32_t key)
{
    return key == nfc_key::Left || key == 'z' || key == 'Z';
}

bool isNextKey(uint32_t key)
{
    return key == nfc_key::Right || key == 'c' || key == 'C';
}

bool isScrollUpKey(uint32_t key)
{
    return key == nfc_key::Up;
}

bool isScrollDownKey(uint32_t key)
{
    return key == nfc_key::Down;
}

}  // namespace

NfcViewModel::NfcViewModel(NfcRouter& router, NfcModel& model) : ViewModel(router), _model(model)
{
}

void NfcViewModel::onEnter()
{
    _section.set(NfcSection::Scan);
    const auto& status   = _model.readerStatus().get();
    _last_failed_attempt = status.failedAttempt;
    if (status.initializationFailed && status.failedAttempt != 0) {
        showInitializationDialog();
    } else {
        _initialization_dialog_active.set(false);
        _initialization_dialog_modal_active = false;
    }
}

void NfcViewModel::onExit()
{
    _initialization_dialog_active.set(false);
    _initialization_dialog_modal_active = false;
}

void NfcViewModel::onKey(uint32_t key)
{
    if (_initialization_dialog_modal_active) {
        if (_initialization_dialog_active.get() && key == '\x1b') {
            dismissInitializationDialog();
        } else if (_initialization_dialog_active.get() && (key == '\r' || key == 'r' || key == 'R') &&
                   canRetryReader()) {
            retryReader();
        }
        return;
    }

    if (key == '\x1b') {
        if (_section.get() != NfcSection::Scan) {
            setSection(NfcSection::Scan);
        }
        return;
    }

    if (isPreviousKey(key)) {
        selectPreviousSection();
        return;
    }
    if (isNextKey(key)) {
        selectNextSection();
        return;
    }

    const NfcSection current = _section.get();
    if (current != NfcSection::Scan && isScrollUpKey(key)) {
        requestScroll(kDetailScrollStep);
        return;
    }
    if (current != NfcSection::Scan && isScrollDownKey(key)) {
        requestScroll(-kDetailScrollStep);
        return;
    }

    switch (current) {
        case NfcSection::Scan:
            if (key == '\r') {
                if (canRetryReader()) {
                    retryReader();
                } else if (_model.tagSession().get()) {
                    setSection(NfcSection::TagDetail);
                }
            }
            break;
        case NfcSection::TagDetail:
            break;
        case NfcSection::ReaderInfo:
            if ((key == '\r' || key == 'r' || key == 'R') && canRetryReader()) {
                retryReader();
            }
            break;
        case NfcSection::Count:
            break;
    }
}

void NfcViewModel::tick(uint32_t nowMs)
{
    (void)nowMs;
    const auto& status = _model.readerStatus().get();
    if (status.initializationFailed && status.failedAttempt != 0 && status.failedAttempt != _last_failed_attempt) {
        _last_failed_attempt = status.failedAttempt;
        _section.set(NfcSection::Scan);
        showInitializationDialog();
    }
    if (status.ready && _initialization_dialog_active.get()) {
        dismissInitializationDialog();
    }
}

void NfcViewModel::dismissInitializationDialog()
{
    if (!_initialization_dialog_modal_active) {
        return;
    }
    _initialization_dialog_active.set(false);
}

void NfcViewModel::notifyInitializationDialogHidden()
{
    if (_initialization_dialog_modal_active && !_initialization_dialog_active.get()) {
        _initialization_dialog_modal_active = false;
    }
}

void NfcViewModel::retryReader()
{
    if (!canRetryReader()) {
        return;
    }
    if (_model.retry()) {
        if (_initialization_dialog_modal_active) {
            dismissInitializationDialog();
        }
    } else {
        showInitializationDialog();
    }
}

bool NfcViewModel::canRetryReader() const
{
    const auto& status = _model.readerStatus().get();
    return status.state == nfc::ReaderState::Error && !status.ready && !status.backendUnavailable;
}

void NfcViewModel::showInitializationDialog()
{
    _initialization_dialog_modal_active = true;
    _initialization_dialog_active.set(true);
}

void NfcViewModel::setSection(NfcSection section)
{
    if (_section.get() == section || section == NfcSection::Count) {
        return;
    }
    spdlog::info("Cap-CC1101-NFC section -> {}", nfcSectionName(section));
    _section.set(section);
}

void NfcViewModel::selectPreviousSection()
{
    const int current = static_cast<int>(_section.get());
    setSection(static_cast<NfcSection>(std::max(current - 1, 0)));
}

void NfcViewModel::selectNextSection()
{
    const int current = static_cast<int>(_section.get());
    const int last    = static_cast<int>(NfcSection::Count) - 1;
    setSection(static_cast<NfcSection>(std::min(current + 1, last)));
}

void NfcViewModel::requestScroll(int32_t amount)
{
    _scroll_request.set(ScrollRequest{++_scroll_serial, amount});
}

}  // namespace cap_nfc

#pragma once

#include "core/nfc_router.hpp"
#include "models/nfc_model.hpp"
#include "view_models/nfc_view_model.hpp"
#include "views/help_view.hpp"
#include "views/nfc_view.hpp"
#include "views/view.hpp"

#include <array>
#include <lvgl.h>
#include <memory>

namespace cap_nfc {

class NfcApp {
public:
    NfcApp();
    ~NfcApp();

    NfcApp(const NfcApp&)            = delete;
    NfcApp& operator=(const NfcApp&) = delete;

    void start();
    void stop();
    void onKey(uint32_t key);
    bool onLvglKeyState(uint32_t lvKey, const char* utf8, bool pressed);
    void tick(uint32_t nowMs);

    bool quitRequested() const
    {
        return _quit_requested;
    }

private:
    NfcRouter _router;
    NfcModel _model;
    NfcViewModel _nfc_vm;
    NfcView _nfc_view;
    std::unique_ptr<HelpView> _help_view;
    ViewModel* _current_vm    = nullptr;
    View* _current_view       = nullptr;
    lv_group_t* _input_group  = nullptr;
    size_t _route_observer_id = 0;
    bool _quit_requested      = false;
    bool _started             = false;
    bool _help_pressed        = false;
    bool _esc_pressed         = false;
    bool _esc_hold_active     = false;
    bool _esc_hold_hint_shown = false;
    uint32_t _esc_down_ms     = 0;
    lv_obj_t* _esc_hold_hint  = nullptr;

    std::array<ViewModel*, static_cast<size_t>(PageId::Count)> _view_models;
    std::array<View*, static_cast<size_t>(PageId::Count)> _views;

    ViewModel* viewModelFor(PageId page);
    View* viewFor(PageId page);
    void setupInputGroup();
    void setCurrentPage(PageId page);
    void showEscHoldHint();
    void hideEscHoldHint();
    static void onRouteChanged(void* context, const PageId& page);
    static void onKeyboardEvent(lv_event_t* event);
};

}  // namespace cap_nfc

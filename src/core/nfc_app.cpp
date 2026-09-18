#include "core/nfc_app.hpp"

#include <spdlog/spdlog.h>

#if LV_USE_SDL
#include <SDL2/SDL.h>
#endif

namespace cap_nfc {
namespace {

constexpr uint32_t kEscHintDelayMs = 500;

#if LV_USE_SDL
bool sdlHelpKeyHeld()
{
    int keyCount       = 0;
    const Uint8* state = SDL_GetKeyboardState(&keyCount);
    return state && static_cast<int>(SDL_SCANCODE_H) < keyCount && state[SDL_SCANCODE_H] != 0;
}
#endif

bool isTextKey(const char* utf8, char expectedLowercase)
{
    if (!utf8 || utf8[0] == '\0' || utf8[1] != '\0') {
        return false;
    }
    return utf8[0] == expectedLowercase || utf8[0] == expectedLowercase - ('a' - 'A');
}

}  // namespace

NfcApp::NfcApp() : _nfc_vm(_router, _model), _nfc_view(_nfc_vm), _view_models{&_nfc_vm}, _views{&_nfc_view}
{
}

NfcApp::~NfcApp()
{
    stop();
}

void NfcApp::start()
{
    if (_started) {
        return;
    }

    spdlog::info("NfcApp: start");
    _started        = true;
    _quit_requested = false;
    _help_pressed   = false;
    _esc_hold_active = false;
    _esc_hold_hint_shown = false;
    _esc_down_ms = 0;
    _esc_hold_hint = nullptr;
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, LV_PART_MAIN);
    _help_view = std::make_unique<HelpView>(lv_screen_active());
    setupInputGroup();
    _model.start();
    _route_observer_id = _router.currentPage().observe(this, onRouteChanged);
    setCurrentPage(_router.page());
}

void NfcApp::stop()
{
    if (!_started) {
        return;
    }

    spdlog::info("NfcApp: stopping NFC model");
    if (_help_view) {
        _help_view->hide();
    }
    _model.stop();
    spdlog::info("NfcApp: NFC model stopped; releasing UI");

    if (_route_observer_id != 0) {
        _router.currentPage().removeObserver(_route_observer_id);
        _route_observer_id = 0;
    }
    if (_current_view) {
        _current_view->onExit();
        _current_view = nullptr;
    }
    if (_current_vm) {
        _current_vm->onExit();
        _current_vm = nullptr;
    }
    hideEscHoldHint();
    _help_view.reset();
    if (_input_group) {
#if LV_USE_SDL
        lv_indev_t* inputDevice = lv_indev_get_next(nullptr);
        while (inputDevice) {
            if (lv_indev_get_type(inputDevice) == LV_INDEV_TYPE_KEYPAD) {
                lv_indev_remove_event_cb_with_user_data(inputDevice, onKeyboardEvent, this);
            }
            inputDevice = lv_indev_get_next(inputDevice);
        }
#endif
        lv_group_del(_input_group);
        _input_group = nullptr;
    }
    _started      = false;
    _help_pressed = false;
    _esc_hold_active = false;
    _esc_hold_hint_shown = false;
    _esc_down_ms = 0;
    spdlog::info("NfcApp: stop complete");
}

void NfcApp::showEscHoldHint()
{
    if (_esc_hold_hint) {
        return;
    }

    _esc_hold_hint = lv_obj_create(lv_layer_top());
    if (!_esc_hold_hint) {
        return;
    }
    lv_obj_remove_style_all(_esc_hold_hint);
    lv_obj_set_size(_esc_hold_hint, 224, 30);
    lv_obj_align(_esc_hold_hint, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_color(_esc_hold_hint, lv_color_hex(0x1B1E24), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_esc_hold_hint, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_esc_hold_hint, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(_esc_hold_hint, lv_color_hex(0x5A6070), LV_PART_MAIN);
    lv_obj_set_style_radius(_esc_hold_hint, 4, LV_PART_MAIN);

    lv_obj_t* label = lv_label_create(_esc_hold_hint);
    lv_label_set_text(label, "Hold ESC 3s to return home");
    lv_obj_set_style_text_color(label, lv_color_hex(0xF2F4F7), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_center(label);
    lv_obj_move_foreground(_esc_hold_hint);
}

void NfcApp::hideEscHoldHint()
{
    if (!_esc_hold_hint) {
        return;
    }
    lv_obj_del(_esc_hold_hint);
    _esc_hold_hint = nullptr;
}

void NfcApp::onKey(uint32_t key)
{
    if (key == nfc_key::Help && _help_view) {
        _help_view->toggle();
        return;
    }

    if (_help_view && _help_view->visible()) {
        if (key == '\x1b') {
            _help_view->hide();
        }
        return;
    }

    if (key == '\x1b' && _router.page() == PageId::Nfc && _nfc_vm.atRoot() && !_nfc_vm.modalActive()) {
        spdlog::info("NfcApp: quit requested");
        _quit_requested = true;
        return;
    }

    if (_current_vm) {
        _current_vm->onKey(key);
    }
}

bool NfcApp::onLvglKeyState(uint32_t lvKey, const char* utf8, bool pressed)
{
#if LV_USE_SDL
    const bool desktopHelp = isTextKey(utf8, 'h');
#else
    const bool desktopHelp = false;
#endif
    if (lvKey == nfc_key::Help || desktopHelp) {
#if LV_USE_SDL
        if (!pressed && desktopHelp && sdlHelpKeyHeld()) {
            return true;
        }
#endif
        if (pressed && !_help_pressed) {
            onKey(nfc_key::Help);
        }
        _help_pressed = pressed;
        return true;
    }

    if (lvKey == LV_KEY_ESC) {
        if (!pressed) {
            if (_esc_hold_active) {
                _esc_hold_active = false;
                _esc_hold_hint_shown = false;
                hideEscHoldHint();
            }
            return true;
        }
        if (_help_view && _help_view->visible()) {
            onKey('\x1b');
            return true;
        }
        if (_router.page() == PageId::Nfc && _nfc_vm.atRoot() && !_nfc_vm.modalActive()) {
            _esc_hold_active = true;
            _esc_hold_hint_shown = false;
            _esc_down_ms = lv_tick_get();
            return true;
        }
        onKey('\x1b');
        return true;
    }

    if (!pressed) {
        return true;
    }

    switch (lvKey) {
        case LV_KEY_ENTER:
            onKey('\r');
            return true;
        case LV_KEY_UP:
            onKey(nfc_key::Up);
            return true;
        case LV_KEY_DOWN:
            onKey(nfc_key::Down);
            return true;
        case LV_KEY_LEFT:
        case LV_KEY_PREV:
            onKey(nfc_key::Left);
            return true;
        case LV_KEY_RIGHT:
        case LV_KEY_NEXT:
            onKey(nfc_key::Right);
            return true;
        default:
            break;
    }

    if (isTextKey(utf8, 'f')) {
        onKey(nfc_key::Up);
    } else if (isTextKey(utf8, 'x')) {
        onKey(nfc_key::Down);
    } else if (isTextKey(utf8, 'z')) {
        onKey(nfc_key::Left);
    } else if (isTextKey(utf8, 'c')) {
        onKey(nfc_key::Right);
    } else if (utf8 && utf8[0] >= 0x20 && utf8[0] < 0x7f && utf8[1] == '\0') {
        onKey(static_cast<uint8_t>(utf8[0]));
    }
    return true;
}

void NfcApp::tick(uint32_t nowMs)
{
#if LV_USE_SDL
    if (_help_pressed && !sdlHelpKeyHeld()) {
        _help_pressed = false;
    }
#endif
    _model.tick(nowMs);
    if (_current_vm) {
        _current_vm->tick(nowMs);
    }
    if (_current_view) {
        _current_view->tick(nowMs);
    }
    if (_esc_hold_active) {
        if (_router.page() != PageId::Nfc || !_nfc_vm.atRoot() || _nfc_vm.modalActive()) {
            _esc_hold_active = false;
            _esc_hold_hint_shown = false;
            hideEscHoldHint();
        } else if (!_esc_hold_hint_shown && nowMs - _esc_down_ms >= kEscHintDelayMs) {
            _esc_hold_hint_shown = true;
            showEscHoldHint();
        }
    }
}

ViewModel* NfcApp::viewModelFor(PageId page)
{
    for (auto* viewModel : _view_models) {
        if (viewModel && viewModel->pageId() == page) {
            return viewModel;
        }
    }
    return nullptr;
}

View* NfcApp::viewFor(PageId page)
{
    const auto index = static_cast<size_t>(page);
    return index < _views.size() ? _views[index] : nullptr;
}

void NfcApp::setupInputGroup()
{
    if (_input_group) {
        return;
    }

    _input_group            = lv_group_create();
    lv_indev_t* inputDevice = lv_indev_get_next(nullptr);
    while (inputDevice) {
        if (lv_indev_get_type(inputDevice) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_group(inputDevice, _input_group);
#if LV_USE_SDL
            lv_indev_add_event_cb(inputDevice, onKeyboardEvent, LV_EVENT_KEY, this);
            lv_indev_add_event_cb(inputDevice, onKeyboardEvent, LV_EVENT_RELEASED, this);
#endif
        }
        inputDevice = lv_indev_get_next(inputDevice);
    }
}

void NfcApp::setCurrentPage(PageId page)
{
    ViewModel* nextViewModel = viewModelFor(page);
    View* nextView           = viewFor(page);
    if (!nextViewModel || !nextView || (nextViewModel == _current_vm && nextView == _current_view)) {
        return;
    }

    if (_current_view) {
        _current_view->onExit();
    }
    if (_current_vm) {
        _current_vm->onExit();
    }

    _current_vm   = nextViewModel;
    _current_view = nextView;
    spdlog::info("Cap-CC1101-NFC route -> {}", pageIdName(page));
    _current_vm->onEnter();
    _current_view->onEnter(lv_screen_active());
}

void NfcApp::onRouteChanged(void* context, const PageId& page)
{
    auto* self = static_cast<NfcApp*>(context);
    if (self) {
        self->setCurrentPage(page);
    }
}

void NfcApp::onKeyboardEvent(lv_event_t* event)
{
    auto* self        = static_cast<NfcApp*>(lv_event_get_user_data(event));
    auto* inputDevice = static_cast<lv_indev_t*>(lv_event_get_target(event));
    if (!self || !inputDevice) {
        return;
    }

    const uint32_t key = lv_indev_get_key(inputDevice);
    char utf8[2]       = {0, 0};
    if (key >= 0x20 && key < 0x7f) {
        utf8[0] = static_cast<char>(key);
    }
    const bool pressed =
        lv_event_get_code(event) == LV_EVENT_KEY && lv_indev_get_state(inputDevice) == LV_INDEV_STATE_PRESSED;
    self->onLvglKeyState(key, utf8, pressed);
}

}  // namespace cap_nfc

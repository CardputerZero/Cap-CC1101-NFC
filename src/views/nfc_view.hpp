#pragma once

#include "view_models/nfc_view_model.hpp"
#include "views/view.hpp"

#include <lvgl/lvgl_cpp/obj.hpp>
#include <memory>
#include <optional>

namespace cap_nfc {

class NfcView : public View {
public:
    explicit NfcView(NfcViewModel& viewModel);
    ~NfcView() override;

    void onEnter(lv_obj_t* parent) override;
    void onExit() override;
    void tick(uint32_t nowMs) override;

private:
    class NfcPager;

    NfcViewModel& _view_model;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    std::unique_ptr<NfcPager> _pager;
    uint32_t _scroll_serial_seen = 0;
    bool _title_shown            = false;

    static void onReaderStatusChanged(void* context, const nfc::ReaderStatus& status);
    static void onTagSessionChanged(void* context, const std::optional<nfc::TagSession>& session);
    static void onSectionChanged(void* context, const NfcSection& section);
    static void onScrollRequestChanged(void* context, const ScrollRequest& request);
    static void onInitializationDialogActiveChanged(void* context, const bool& active);
};

}  // namespace cap_nfc

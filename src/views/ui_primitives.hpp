#pragma once

#include "core/nfc_types.hpp"

#include <core/animation/animate_value/animate_value.hpp>
#include <functional>
#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>
#include <array>
#include <memory>
#include <string_view>

namespace cap_nfc::ui {

struct Frame {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

lv_opa_t toOpacity(float value);

class Panel : public smooth_ui_toolkit::lvgl_cpp::Container {
public:
    Panel(lv_obj_t* parent, Frame frame, uint32_t color = 0x000000, lv_opa_t opacity = LV_OPA_TRANSP,
          int32_t radius = 0);
};

class TextLabel : public smooth_ui_toolkit::lvgl_cpp::Label {
public:
    TextLabel(lv_obj_t* parent, std::string_view text, Frame frame, const lv_font_t* font, uint32_t color,
              lv_text_align_t alignment = LV_TEXT_ALIGN_LEFT);
};

class InfoRow {
public:
    InfoRow(lv_obj_t* parent, int32_t y, std::string_view caption);

    void setValue(std::string_view value);
    void setValueColor(uint32_t color);
    void setY(int32_t y);
    void setValueAutoHeight();
    int32_t valueHeight();

private:
    std::unique_ptr<TextLabel> _caption;
    std::unique_ptr<TextLabel> _value;
};

class StatusBadge {
public:
    StatusBadge(lv_obj_t* parent, std::string_view text, uint32_t color);

    void setText(std::string_view text);
    void setColor(uint32_t color);

private:
    std::unique_ptr<Panel> _dot;
    std::unique_ptr<TextLabel> _label;

    void realign();
};

class ScrollBar {
public:
    ScrollBar(lv_obj_t* parent, Frame frame);

    void update(float offset, int32_t visibleHeight, int32_t contentHeight);
    void setHidden(bool hidden);

private:
    std::unique_ptr<Panel> _track;
    std::unique_ptr<Panel> _thumb;
    Frame _frame;
    bool _hidden                 = false;
    bool _scrollable             = false;
    bool _visibility_initialized = false;
    bool _applied_hidden         = false;

    void applyVisibility();
};

class PageIndicator {
public:
    explicit PageIndicator(lv_obj_t* parent);

    void setSection(NfcSection section);
    void setHidden(bool hidden);

private:
    std::unique_ptr<Panel> _panel;
    std::array<std::unique_ptr<Panel>, static_cast<std::size_t>(NfcSection::Count)> _dots;
    bool _hidden = false;
};

class TitleHud {
public:
    explicit TitleHud(lv_obj_t* parent);

    void show();
    void dismiss();
    void tick(uint32_t nowMs);

private:
    std::unique_ptr<Panel> _panel;
    std::unique_ptr<TextLabel> _label;
    smooth_ui_toolkit::AnimateValue _y;
    uint32_t _shown_at_ms = 0;
    bool _started         = false;
    bool _closing         = false;
};

class InitializationDialog {
public:
    InitializationDialog(lv_obj_t* parent, std::function<void()> onClose, std::function<void()> onRetry);
    ~InitializationDialog();

    void setActive(bool active);
    void setBackendUnavailable(bool unavailable);
    void setRetryEnabled(bool enabled);
    void tick(uint32_t nowMs);
    bool hidden() const;

private:
    class ActionButton;

    std::unique_ptr<Panel> _mask;
    std::unique_ptr<Panel> _panel;
    std::unique_ptr<TextLabel> _title;
    std::unique_ptr<TextLabel> _message;
    std::unique_ptr<ActionButton> _close;
    std::unique_ptr<ActionButton> _retry;
    smooth_ui_toolkit::AnimateValue _y;
    bool _active              = false;
    bool _hidden              = true;
    bool _backend_unavailable = false;
    bool _retry_enabled       = true;

    void configureAnimation(float duration, float bounce);
    void updateRetryPresentation();
    void applyAnimatedValue();
};

}  // namespace cap_nfc::ui

#include "views/ui_primitives.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace cap_nfc::ui {
namespace {

constexpr int32_t kTitleHiddenY      = -29;
constexpr int32_t kTitleShownY       = -8;
constexpr uint32_t kTitleHoldMs      = 3200;
constexpr int32_t kDialogWidth       = 286;
constexpr int32_t kDialogHeight      = 124;
constexpr int32_t kDialogHiddenY     = -150;
constexpr uint32_t kActiveDotColor   = 0xE4E4E4;
constexpr uint32_t kInactiveDotColor = 0x4E5157;

}  // namespace

lv_opa_t toOpacity(float value)
{
    return static_cast<lv_opa_t>(std::clamp(static_cast<int32_t>(std::lround(value)), 0, 255));
}

Panel::Panel(lv_obj_t* parent, Frame frame, uint32_t color, lv_opa_t opacity, int32_t radius) : Container(parent)
{
    setPos(frame.x, frame.y);
    setSize(frame.width, frame.height);
    setBgColor(lv_color_hex(color));
    setBgOpa(opacity);
    setRadius(radius);
    setBorderWidth(0);
    setOutlineWidth(0);
    setShadowWidth(0);
    setPaddingAll(0);
    setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    removeFlag(LV_OBJ_FLAG_SCROLLABLE);
}

TextLabel::TextLabel(lv_obj_t* parent, std::string_view text, Frame frame, const lv_font_t* font, uint32_t color,
                     lv_text_align_t alignment)
    : Label(parent)
{
    setText(text);
    setLongMode(LV_LABEL_LONG_MODE_WRAP);
    setPos(frame.x, frame.y);
    setSize(frame.width, frame.height);
    setTextFont(font);
    setTextColor(lv_color_hex(color));
    setTextAlign(alignment);
    setBgOpa(LV_OPA_TRANSP);
    setPaddingAll(0);
}

InfoRow::InfoRow(lv_obj_t* parent, int32_t y, std::string_view caption)
    : _caption(std::make_unique<TextLabel>(parent, caption, Frame{14, y, 82, 16}, &lv_font_montserrat_10, 0x777B82)),
      _value(std::make_unique<TextLabel>(parent, "--", Frame{96, y - 1, 210, 18}, &lv_font_montserrat_12, 0xE4E4E4))
{
}

void InfoRow::setValue(std::string_view value)
{
    _value->setText(value);
}

void InfoRow::setValueColor(uint32_t color)
{
    _value->setTextColor(lv_color_hex(color));
}

void InfoRow::setY(int32_t y)
{
    _caption->setY(y);
    _value->setY(y - 1);
}

void InfoRow::setValueAutoHeight()
{
    // Keep the original single-line row height while allowing wrapped values to grow.
    lv_obj_set_style_min_height(_value->raw_ptr(), 18, LV_PART_MAIN);
    _value->setHeight(LV_SIZE_CONTENT);
}

int32_t InfoRow::valueHeight()
{
    lv_obj_update_layout(_value->raw_ptr());
    return lv_obj_get_height(_value->raw_ptr());
}

StatusBadge::StatusBadge(lv_obj_t* parent, std::string_view text, uint32_t color)
    : _dot(std::make_unique<Panel>(parent, Frame{0, 0, 5, 5}, color, LV_OPA_COVER, LV_RADIUS_CIRCLE)),
      _label(
          std::make_unique<TextLabel>(parent, text, Frame{0, 0, LV_SIZE_CONTENT, 16}, &lv_font_montserrat_10, 0x9A9A9A))
{
    realign();
}

void StatusBadge::setText(std::string_view text)
{
    _label->setText(text);
    realign();
}

void StatusBadge::setColor(uint32_t color)
{
    _dot->setBgColor(lv_color_hex(color));
}

void StatusBadge::realign()
{
    _label->align(LV_ALIGN_TOP_RIGHT, -12, 7);
    lv_obj_update_layout(_label->raw_ptr());
    _dot->alignTo(*_label, LV_ALIGN_OUT_LEFT_MID, -4, -3);
}

ScrollBar::ScrollBar(lv_obj_t* parent, Frame frame)
    : _track(std::make_unique<Panel>(parent, frame, 0x2B2B2B, LV_OPA_COVER, 1)),
      _thumb(std::make_unique<Panel>(parent, Frame{frame.x, frame.y, frame.width, 17}, 0x848484, LV_OPA_COVER, 1)),
      _frame(frame)
{
    applyVisibility();
}

void ScrollBar::update(float offset, int32_t visibleHeight, int32_t contentHeight)
{
    const int32_t maximum = std::max(0, contentHeight - visibleHeight);
    _scrollable           = maximum > 0;
    if (!_scrollable) {
        applyVisibility();
        return;
    }

    constexpr int32_t kMinimumThumbHeight = 17;
    const float progress = std::clamp(offset, 0.0F, static_cast<float>(maximum)) / static_cast<float>(maximum);
    const int32_t thumbHeight =
        std::clamp(static_cast<int32_t>(std::lround(static_cast<float>(visibleHeight) /
                                                    static_cast<float>(contentHeight) * _frame.height)),
                   kMinimumThumbHeight, _frame.height);
    const int32_t travel = _frame.height - thumbHeight;
    const int32_t thumbY = _frame.y + static_cast<int32_t>(std::lround(progress * static_cast<float>(travel)));
    _thumb->setSize(_frame.width, thumbHeight);
    _thumb->setPos(_frame.x, thumbY);
    applyVisibility();
}

void ScrollBar::setHidden(bool hidden)
{
    if (_hidden == hidden) {
        return;
    }
    _hidden = hidden;
    applyVisibility();
}

void ScrollBar::applyVisibility()
{
    const bool hidden = _hidden || !_scrollable;
    if (_visibility_initialized && _applied_hidden == hidden) {
        return;
    }
    _visibility_initialized = true;
    _applied_hidden         = hidden;
    _track->setHidden(hidden);
    _thumb->setHidden(hidden);
}

PageIndicator::PageIndicator(lv_obj_t* parent)
    : _panel(std::make_unique<Panel>(parent, Frame{139, 157, 42, 24}, 0x000000, LV_OPA_COVER, 7))
{
    for (std::size_t index = 0; index < _dots.size(); ++index) {
        _dots[index] = std::make_unique<Panel>(_panel->raw_ptr(), Frame{8 + static_cast<int32_t>(index) * 10, 3, 5, 5},
                                               kInactiveDotColor, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    }
    setSection(NfcSection::Scan);
}

void PageIndicator::setSection(NfcSection section)
{
    const auto active = static_cast<std::size_t>(section);
    for (std::size_t index = 0; index < _dots.size(); ++index) {
        _dots[index]->setBgColor(lv_color_hex(index == active ? kActiveDotColor : kInactiveDotColor));
    }
}

void PageIndicator::setHidden(bool hidden)
{
    if (_hidden == hidden) {
        return;
    }
    _hidden = hidden;
    _panel->setHidden(hidden);
}

TitleHud::TitleHud(lv_obj_t* parent)
    : _panel(std::make_unique<Panel>(parent, Frame{103, kTitleHiddenY, 114, 28}, 0x0B0C0E, LV_OPA_COVER, 8)),
      _label(std::make_unique<TextLabel>(_panel->raw_ptr(), "NFC Reader", Frame{0, 8, 114, 18}, &lv_font_montserrat_14,
                                         0xE4E4E4, LV_TEXT_ALIGN_CENTER)),
      _y(kTitleHiddenY)
{
    _panel->setBorderWidth(1);
    _panel->setBorderColor(lv_color_hex(0x55585E));
    _panel->setHidden(true);
}

void TitleHud::show()
{
    if (_started) {
        return;
    }

    _started = true;
    _panel->setHidden(false);
    _y.springOptions().visualDuration = 0.4F;
    _y.springOptions().bounce         = 0.22F;
    _y.teleport(kTitleHiddenY);
    _y.move(kTitleShownY);
}

void TitleHud::dismiss()
{
    if (!_started || _closing) {
        return;
    }

    _closing                          = true;
    _y.springOptions().visualDuration = 0.34F;
    _y.springOptions().bounce         = 0.0F;
    _y.move(kTitleHiddenY);
}

void TitleHud::tick(uint32_t nowMs)
{
    if (!_started) {
        return;
    }

    if (_shown_at_ms == 0) {
        _shown_at_ms = nowMs;
    }
    if (!_closing && nowMs - _shown_at_ms >= kTitleHoldMs) {
        dismiss();
    }

    if (!_y.done()) {
        _y.update(static_cast<float>(nowMs) / 1000.0F);
        _panel->setY(static_cast<int32_t>(std::lround(_y.directValue())));
    }
    if (_closing && _y.done()) {
        _panel->setHidden(true);
        _started     = false;
        _closing     = false;
        _shown_at_ms = 0;
    }
}

class InitializationDialog::ActionButton {
public:
    ActionButton(lv_obj_t* parent, Frame frame, std::string_view text, uint32_t background, uint32_t foreground,
                 std::function<void()> action)
        : _button(std::make_unique<Panel>(parent, frame, background, LV_OPA_COVER, 5)),
          _label(std::make_unique<TextLabel>(_button->raw_ptr(), text, Frame{0, 0, frame.width, LV_SIZE_CONTENT},
                                             &lv_font_montserrat_14, foreground, LV_TEXT_ALIGN_CENTER)),
          _action(std::move(action)),
          _background(background),
          _foreground(foreground)
    {
        _button->addFlag(LV_OBJ_FLAG_CLICKABLE);
        _button->onClick().connect([this]() {
            if (_enabled && _action) {
                _action();
            }
        });
        _label->align(LV_ALIGN_CENTER, 0, 0);
    }

    void align(lv_align_t alignment, int32_t x, int32_t y)
    {
        _button->align(alignment, x, y);
    }

    void setHidden(bool hidden)
    {
        _button->setHidden(hidden);
    }

    void setEnabled(bool enabled)
    {
        _enabled = enabled;
        if (_enabled) {
            _button->addFlag(LV_OBJ_FLAG_CLICKABLE);
            _button->setBgColor(lv_color_hex(_background));
            _label->setTextColor(lv_color_hex(_foreground));
        } else {
            _button->removeFlag(LV_OBJ_FLAG_CLICKABLE);
            _button->setBgColor(lv_color_hex(0x666666));
            _label->setTextColor(lv_color_hex(0xA0A0A0));
        }
    }

private:
    std::unique_ptr<Panel> _button;
    std::unique_ptr<TextLabel> _label;
    std::function<void()> _action;
    uint32_t _background;
    uint32_t _foreground;
    bool _enabled = true;
};

InitializationDialog::~InitializationDialog() = default;

InitializationDialog::InitializationDialog(lv_obj_t* parent, std::function<void()> onClose,
                                           std::function<void()> onRetry)
    : _mask(std::make_unique<Panel>(parent, Frame{0, 0, 320, 170}, 0x000000, 176)),
      _panel(std::make_unique<Panel>(parent, Frame{0, 0, kDialogWidth, kDialogHeight}, 0x474747, LV_OPA_COVER, 14)),
      _title(std::make_unique<TextLabel>(_panel->raw_ptr(), "NFC Cap unavailable", Frame{0, 0, 260, 18},
                                         &lv_font_montserrat_14, 0xF3F3F3, LV_TEXT_ALIGN_CENTER)),
      _message(std::make_unique<TextLabel>(_panel->raw_ptr(), "Connect or reseat the Cap,\nthen retry initialization.",
                                           Frame{0, 0, 254, 36}, &lv_font_montserrat_12, 0xBCBCBC,
                                           LV_TEXT_ALIGN_CENTER)),
      _close(std::make_unique<ActionButton>(_panel->raw_ptr(), Frame{0, 0, 104, 23}, "ESC: Close", 0x6D6D6D, 0xF3F3F3,
                                            std::move(onClose))),
      _retry(std::make_unique<ActionButton>(_panel->raw_ptr(), Frame{0, 0, 112, 23}, "Enter: Retry", 0xFED40D, 0x5E4D00,
                                            std::move(onRetry))),
      _y(kDialogHiddenY)
{
    _title->align(LV_ALIGN_CENTER, 0, -42);
    _message->align(LV_ALIGN_CENTER, 0, -8);
    _close->align(LV_ALIGN_CENTER, -61, 39);
    _retry->align(LV_ALIGN_CENTER, 57, 39);
    applyAnimatedValue();
    _mask->setHidden(true);
    _panel->setHidden(true);
}

void InitializationDialog::setActive(bool active)
{
    if (active == _active) {
        return;
    }

    _active = active;
    if (_active) {
        const bool wasHidden = _hidden;
        _hidden              = false;
        _mask->setHidden(false);
        _panel->setHidden(false);
        _mask->moveForeground();
        _panel->moveForeground();
        configureAnimation(0.35F, 0.24F);
        if (wasHidden) {
            _y.teleport(kDialogHiddenY);
            applyAnimatedValue();
        }
        _y.move(0);
        return;
    }

    configureAnimation(0.28F, 0.0F);
    _y.move(kDialogHiddenY);
}

void InitializationDialog::setBackendUnavailable(bool unavailable)
{
    _backend_unavailable = unavailable;
    if (unavailable) {
        _title->setText("NFC backend unavailable");
        _message->setText("This build does not include the\nST25R3916 hardware backend.");
        _close->align(LV_ALIGN_CENTER, 0, 39);
        updateRetryPresentation();
        return;
    }

    _title->setText("NFC Cap unavailable");
    _message->setText("Connect or reseat the Cap,\nthen retry initialization.");
    _close->align(LV_ALIGN_CENTER, -61, 39);
    updateRetryPresentation();
}

void InitializationDialog::setRetryEnabled(bool enabled)
{
    _retry_enabled = enabled;
    updateRetryPresentation();
}

void InitializationDialog::tick(uint32_t nowMs)
{
    if (_hidden && !_active) {
        return;
    }
    if (!_y.done()) {
        _y.update(static_cast<float>(nowMs) / 1000.0F);
        applyAnimatedValue();
    }
    if (!_active && _y.done() && !_hidden) {
        _mask->setHidden(true);
        _panel->setHidden(true);
        _hidden = true;
    }
}

bool InitializationDialog::hidden() const
{
    return _hidden;
}

void InitializationDialog::configureAnimation(float duration, float bounce)
{
    _y.springOptions().visualDuration = duration;
    _y.springOptions().bounce         = bounce;
}

void InitializationDialog::updateRetryPresentation()
{
    _retry->setHidden(_backend_unavailable);
    _retry->setEnabled(_retry_enabled && !_backend_unavailable);
}

void InitializationDialog::applyAnimatedValue()
{
    _panel->align(LV_ALIGN_CENTER, 0, static_cast<int32_t>(std::lround(_y.directValue())));
}

}  // namespace cap_nfc::ui

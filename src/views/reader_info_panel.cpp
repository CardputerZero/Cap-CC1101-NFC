#include "views/reader_info_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace cap_nfc {
namespace {

uint32_t readerColor(const nfc::ReaderStatus& status)
{
    if (status.state == nfc::ReaderState::Error) {
        return 0xE06C75;
    }
    if (status.state == nfc::ReaderState::Initializing) {
        return 0xFED40D;
    }
    return status.ready ? 0x3FCC75 : 0x777B82;
}

std::string chipText(const nfc::ReaderInfo& info)
{
    if (info.chipName.empty()) {
        return "--";
    }
    return info.chipVersion.empty() ? info.chipName : info.chipName + " / " + info.chipVersion;
}

}  // namespace

ReaderInfoPanel::ReaderInfoPanel(lv_obj_t* parent)
    : _root(std::make_unique<ui::Panel>(parent, ui::Frame{0, 0, 320, 170})),
      _title(std::make_unique<ui::TextLabel>(_root->raw_ptr(), "READER INFO", ui::Frame{12, 8, 130, 18},
                                             &lv_font_montserrat_14, 0xE4E4E4)),
      _status(std::make_unique<ui::StatusBadge>(_root->raw_ptr(), "STOPPED", 0x777B82)),
      _viewport(std::make_unique<ui::Panel>(_root->raw_ptr(), ui::Frame{0, kViewportY, 320, kViewportHeight})),
      _content(std::make_unique<ui::Panel>(_viewport->raw_ptr(), ui::Frame{0, 0, 320, kViewportHeight})),
      _backend(std::make_unique<ui::InfoRow>(_content->raw_ptr(), kRowStartY, "BACKEND")),
      _chip(std::make_unique<ui::InfoRow>(_content->raw_ptr(), 24, "CHIP")),
      _transport(std::make_unique<ui::InfoRow>(_content->raw_ptr(), 46, "SPI")),
      _irq(std::make_unique<ui::InfoRow>(_content->raw_ptr(), 68, "IRQ")),
      _power(std::make_unique<ui::InfoRow>(_content->raw_ptr(), 90, "POWER")),
      _protocols(std::make_unique<ui::InfoRow>(_content->raw_ptr(), 112, "PROTOCOLS")),
      _divider(std::make_unique<ui::Panel>(_content->raw_ptr(), ui::Frame{14, 138, 292, 1}, 0x2A2C30, LV_OPA_COVER)),
      _diagnostics_caption(std::make_unique<ui::TextLabel>(
          _content->raw_ptr(), "DIAGNOSTICS", ui::Frame{14, 148, 120, 14}, &lv_font_montserrat_10, 0x777B82)),
      _diagnostics(std::make_unique<ui::TextLabel>(_content->raw_ptr(), "Reader stopped",
                                                   ui::Frame{14, 167, 292, LV_SIZE_CONTENT}, &lv_font_montserrat_12,
                                                   0xE4E4E4)),
      _scrollbar(
          std::make_unique<ui::ScrollBar>(_root->raw_ptr(), ui::Frame{314, kViewportY + 4, 3, kViewportHeight - 8}))
{
    _scroll.springOptions().visualDuration = 0.38F;
    _scroll.springOptions().bounce         = 0.0F;
    _scroll.teleport(0);
    _backend->setValueAutoHeight();
    _chip->setValueAutoHeight();
    _transport->setValueAutoHeight();
    _irq->setValueAutoHeight();
    _power->setValueAutoHeight();
    _protocols->setValueAutoHeight();
    updateContentHeight();
}

void ReaderInfoPanel::setReaderStatus(const nfc::ReaderStatus& status)
{
    const uint32_t color = readerColor(status);
    _status->setColor(color);
    _status->setText(nfc::readerStateName(status.state));
    _backend->setValue(status.info.backendName.empty() ? "--" : status.info.backendName);
    _chip->setValue(chipText(status.info));
    _transport->setValue(status.info.transport.empty() ? "--" : status.info.transport);
    _irq->setValue(status.info.irq.empty() ? "--" : status.info.irq);
    _power->setValue(status.info.power.empty() ? "--" : status.info.power);
    _protocols->setValue(status.info.protocols.empty() ? "--" : status.info.protocols);
    _diagnostics->setText(status.diagnostics);
    _diagnostics->setTextColor(lv_color_hex(status.state == nfc::ReaderState::Error ? 0xE06C75 : 0xE4E4E4));
    updateContentHeight();
}

void ReaderInfoPanel::scrollBy(int32_t amount)
{
    const float maximum = static_cast<float>(std::max(0, _content_height - kViewportHeight));
    _scroll_target      = std::clamp(_scroll_target - static_cast<float>(amount), 0.0F, maximum);
    _scroll.move(_scroll_target);
}

void ReaderInfoPanel::setOpacity(lv_opa_t opacity)
{
    if (_applied_opacity == opacity) {
        return;
    }
    _applied_opacity = opacity;
    _root->setOpa(opacity);
}

void ReaderInfoPanel::setHidden(bool hidden)
{
    if (_hidden == hidden) {
        return;
    }
    _hidden = hidden;
    _root->setHidden(hidden);
}

void ReaderInfoPanel::setScrollbarHidden(bool hidden)
{
    _scrollbar->setHidden(hidden);
}

void ReaderInfoPanel::tick(uint32_t nowMs)
{
    if (_scroll.done()) {
        return;
    }
    _scroll.update(static_cast<float>(nowMs) / 1000.0F);
    applyScroll();
}

void ReaderInfoPanel::updateContentHeight()
{
    layoutRows();
    lv_obj_update_layout(_diagnostics->raw_ptr());
    const int32_t diagnosticsY = lv_obj_get_y(_diagnostics->raw_ptr());
    _content_height = std::max(kViewportHeight, diagnosticsY + lv_obj_get_height(_diagnostics->raw_ptr()) + 14);
    _content->setHeight(_content_height);
    const float maximum = static_cast<float>(std::max(0, _content_height - kViewportHeight));
    if (_scroll_target > maximum) {
        _scroll_target = maximum;
        _scroll.move(_scroll_target);
    }
    applyScroll();
}

void ReaderInfoPanel::layoutRows()
{
    int32_t nextY                          = kRowStartY;
    const std::array<ui::InfoRow*, 6> rows = {_backend.get(), _chip.get(),  _transport.get(),
                                              _irq.get(),     _power.get(), _protocols.get()};
    for (ui::InfoRow* row : rows) {
        row->setY(nextY);
        const int32_t valueHeight = std::max(kRowStep - kRowGap, row->valueHeight());
        nextY += std::max(kRowStep, valueHeight + kRowGap);
    }

    const int32_t dividerY = nextY + 4;
    _divider->setY(dividerY);
    _diagnostics_caption->setY(dividerY + 10);
    _diagnostics->setY(dividerY + 29);
}

void ReaderInfoPanel::applyScroll()
{
    _content->setY(-static_cast<int32_t>(std::lround(_scroll.directValue())));
    _scrollbar->update(_scroll.directValue(), kViewportHeight, _content_height);
}

}  // namespace cap_nfc

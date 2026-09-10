#include "views/scan_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace cap_nfc {
namespace {

constexpr int32_t kContentOffsetY = 7;

uint32_t stateColor(const nfc::ReaderStatus& status, const std::optional<nfc::TagSession>& session)
{
    if (status.state == nfc::ReaderState::Error) {
        return 0xE06C75;
    }
    if (session && session->present) {
        return 0x3FCC75;
    }
    if (status.state == nfc::ReaderState::Initializing) {
        return 0xFED40D;
    }
    return status.ready ? 0x63B3ED : 0x777B82;
}

std::string recordSummary(const nfc::TagSession& session)
{
    if (!session.snapshot.ndefSupported) {
        return session.present ? "NO NDEF / PRESENT" : "NO NDEF / REMOVED";
    }

    char text[64] = {};
    std::snprintf(text, sizeof(text), "%zu NDEF RECORD%s / %s", session.snapshot.records.size(),
                  session.snapshot.records.size() == 1 ? "" : "S", session.present ? "PRESENT" : "REMOVED");
    return text;
}

}  // namespace

ScanPanel::ScanPanel(lv_obj_t* parent)
    : _root(std::make_unique<ui::Panel>(parent, ui::Frame{0, 0, 320, 170})),
      _title(std::make_unique<ui::TextLabel>(_root->raw_ptr(), "SCAN", ui::Frame{12, 8, 80, 18}, &lv_font_montserrat_14,
                                             0xE4E4E4)),
      _status_badge(std::make_unique<ui::StatusBadge>(_root->raw_ptr(), "STARTING", 0xFED40D)),
      _antenna_dot(std::make_unique<ui::Panel>(_root->raw_ptr(), ui::Frame{66, 80 + kContentOffsetY, 8, 8}, 0x63B3ED,
                                               LV_OPA_COVER, LV_RADIUS_CIRCLE)),
      _divider(std::make_unique<ui::Panel>(_root->raw_ptr(), ui::Frame{128, 35 + kContentOffsetY, 1, 94}, 0x2A2C30,
                                           LV_OPA_COVER)),
      _technology(std::make_unique<ui::TextLabel>(_root->raw_ptr(), "WAITING",
                                                  ui::Frame{146, 40 + kContentOffsetY, 162, 20}, &lv_font_montserrat_14,
                                                  0xE4E4E4)),
      _uid_caption(std::make_unique<ui::TextLabel>(
          _root->raw_ptr(), "UID", ui::Frame{146, 65 + kContentOffsetY, 40, 14}, &lv_font_montserrat_10, 0x777B82)),
      _uid(std::make_unique<ui::TextLabel>(_root->raw_ptr(), "--", ui::Frame{146, 80 + kContentOffsetY, 162, 18},
                                           &lv_font_montserrat_12, 0xFFFFFF)),
      _type(std::make_unique<ui::TextLabel>(_root->raw_ptr(), "No tag detected",
                                            ui::Frame{146, kTypeBaseY, 162, LV_SIZE_CONTENT}, &lv_font_montserrat_10,
                                            0x9A9A9A)),
      _summary(std::make_unique<ui::TextLabel>(_root->raw_ptr(), "INITIALIZING", ui::Frame{146, kSummaryBaseY, 162, 15},
                                               &lv_font_montserrat_10, 0x63B3ED))
{
    constexpr std::array<int32_t, 3> kRingSize = {78, 56, 34};
    for (std::size_t index = 0; index < _rings.size(); ++index) {
        const int32_t size = kRingSize[index];
        _rings[index]      = std::make_unique<ui::Panel>(
            _root->raw_ptr(), ui::Frame{70 - size / 2, 84 + kContentOffsetY - size / 2, size, size});
        _rings[index]->setRadius(LV_RADIUS_CIRCLE);
        _rings[index]->setBorderWidth(2);
        _rings[index]->setBorderColor(lv_color_hex(0x63B3ED));
        _rings[index]->setOpa(static_cast<lv_opa_t>(120 - index * 20));
    }
    _type->setLongMode(LV_LABEL_LONG_MODE_WRAP);
    _type->setWidth(162);
    _type->setHeight(LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(_type->raw_ptr(), kTypeBaseHeight, LV_PART_MAIN);
    updateTypeLayout();
    refresh();
}

void ScanPanel::setReaderStatus(const nfc::ReaderStatus& status)
{
    _status = status;
    refresh();
}

void ScanPanel::setTagSession(const std::optional<nfc::TagSession>& session)
{
    _session = session;
    refresh();
}

void ScanPanel::setOpacity(lv_opa_t opacity)
{
    if (_applied_opacity == opacity) {
        return;
    }
    _applied_opacity = opacity;
    _root->setOpa(opacity);
}

void ScanPanel::setHidden(bool hidden)
{
    if (_hidden == hidden) {
        return;
    }
    _hidden = hidden;
    _root->setHidden(hidden);
}

void ScanPanel::tick(uint32_t nowMs)
{
    const uint32_t color = stateColor(_status, _session);
    const bool animate   = _status.state == nfc::ReaderState::Scanning && !(_session && _session->present);
    for (std::size_t index = 0; index < _rings.size(); ++index) {
        float opacity = 126.0F;
        if (animate) {
            const float phase =
                static_cast<float>(nowMs % 1800U) / 1800.0F * 6.2831853F + static_cast<float>(index) * 1.35F;
            opacity = 72.0F + (std::sin(phase) + 1.0F) * 54.0F;
        } else if (_session && _session->present) {
            opacity = 168.0F - static_cast<float>(index) * 24.0F;
        }
        _rings[index]->setBorderColor(lv_color_hex(color));
        _rings[index]->setOpa(ui::toOpacity(opacity));
    }
    _antenna_dot->setBgColor(lv_color_hex(color));
}

void ScanPanel::refresh()
{
    const uint32_t color = stateColor(_status, _session);
    _status_badge->setColor(color);
    _summary->setTextColor(lv_color_hex(color));
    for (std::size_t index = 0; index < _rings.size(); ++index) {
        _rings[index]->setBorderColor(lv_color_hex(color));
        _rings[index]->setOpa(_session && _session->present ? static_cast<lv_opa_t>(168 - index * 24)
                                                            : static_cast<lv_opa_t>(126));
    }
    _antenna_dot->setBgColor(lv_color_hex(color));

    if (_status.state == nfc::ReaderState::Error) {
        _status_badge->setText("ERROR");
    } else if (_status.state == nfc::ReaderState::Initializing) {
        _status_badge->setText("STARTING");
    } else if (_session && _session->present) {
        _status_badge->setText("PRESENT");
    } else if (_status.ready) {
        _status_badge->setText("SCANNING");
    } else {
        _status_badge->setText("OFFLINE");
    }

    if (_session) {
        _technology->setText(nfc::tagTechnologyName(_session->snapshot.technology));
        const bool compactUid = _session->snapshot.uid.size() > 7;
        _uid->setTextFont(compactUid ? &lv_font_montserrat_10 : &lv_font_montserrat_12);
        _uid->setText(nfc::bytesToHex(_session->snapshot.uid, compactUid ? "" : " "));
        _type->setText(_session->snapshot.typeName.empty() ? "Unknown tag type" : _session->snapshot.typeName);
        _summary->setText(recordSummary(*_session));
        updateTypeLayout();
        return;
    }

    _technology->setText(_status.state == nfc::ReaderState::Error ? "UNAVAILABLE" : "WAITING");
    _uid->setTextFont(&lv_font_montserrat_12);
    _uid->setText("--");
    if (_status.backendUnavailable) {
        _type->setText("Hardware backend not included");
        _summary->setText("BACKEND NOT BUILT");
    } else {
        _type->setText(_status.state == nfc::ReaderState::Error ? "Reader initialization failed" : "No tag detected");
        _summary->setText(_status.state == nfc::ReaderState::Error ? "RETRY REQUIRED" : "NFC-A/F READY");
    }
    updateTypeLayout();
}

void ScanPanel::updateTypeLayout()
{
    lv_obj_update_layout(_type->raw_ptr());
    const int32_t typeHeight = std::max(kTypeBaseHeight, lv_obj_get_height(_type->raw_ptr()));
    _summary->setY(kSummaryBaseY + typeHeight - kTypeBaseHeight);
}

}  // namespace cap_nfc

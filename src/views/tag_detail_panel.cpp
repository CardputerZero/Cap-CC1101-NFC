#include "views/tag_detail_panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace cap_nfc {
namespace {

std::string atqaSakText(const nfc::TagSnapshot& tag)
{
    if (tag.technology != nfc::TagTechnology::NfcA || tag.atqa.empty()) {
        return "N/A";
    }

    char text[64] = {};
    std::snprintf(text, sizeof(text), "%s / SAK %02X", nfc::bytesToHex(tag.atqa).c_str(),
                  static_cast<unsigned>(tag.sak));
    return text;
}

std::string ndefText(const nfc::TagSnapshot& tag)
{
    if (!tag.ndefSupported) {
        return "Not supported";
    }
    if (!tag.ndefReadable) {
        return "Not readable";
    }
    return "Readable / " + std::to_string(tag.ndefCapacity) + " B";
}

std::string recordsText(const nfc::TagSnapshot& tag)
{
    if (tag.records.empty()) {
        return tag.ndefSupported ? "No NDEF records" : "No NDEF data";
    }

    std::string text;
    for (std::size_t index = 0; index < tag.records.size(); ++index) {
        const auto& record = tag.records[index];
        if (index != 0) {
            text += "\n\n";
        }
        text += nfc::ndefRecordKindName(record.kind);
        if (!record.type.empty()) {
            text += "  ";
            text += record.type;
        }
        text += "\n";
        text += record.value.empty() ? nfc::bytesToHex(record.payload) : record.value;
    }
    return text;
}

}  // namespace

TagDetailPanel::TagDetailPanel(lv_obj_t* parent)
    : _root(std::make_unique<ui::Panel>(parent, ui::Frame{0, 0, 320, 170})),
      _title(std::make_unique<ui::TextLabel>(_root->raw_ptr(), "TAG DETAIL", ui::Frame{12, 8, 130, 18},
                                             &lv_font_montserrat_14, 0xE4E4E4)),
      _status(std::make_unique<ui::StatusBadge>(_root->raw_ptr(), "NO TAG", 0x777B82)),
      _viewport(std::make_unique<ui::Panel>(_root->raw_ptr(), ui::Frame{0, kViewportY, 320, kViewportHeight})),
      _content(std::make_unique<ui::Panel>(_viewport->raw_ptr(), ui::Frame{0, 0, 320, kViewportHeight})),
      _technology(std::make_unique<ui::InfoRow>(_content->raw_ptr(), 2, "TECH")),
      _uid(std::make_unique<ui::InfoRow>(_content->raw_ptr(), 24, "UID")),
      _atqa_sak(std::make_unique<ui::InfoRow>(_content->raw_ptr(), 46, "ATQA / SAK")),
      _type(std::make_unique<ui::InfoRow>(_content->raw_ptr(), 68, "TYPE")),
      _ndef(std::make_unique<ui::InfoRow>(_content->raw_ptr(), 90, "NDEF")),
      _divider(std::make_unique<ui::Panel>(_content->raw_ptr(), ui::Frame{14, 116, 292, 1}, 0x2A2C30, LV_OPA_COVER)),
      _records_caption(std::make_unique<ui::TextLabel>(_content->raw_ptr(), "RECORDS", ui::Frame{14, 126, 100, 14},
                                                       &lv_font_montserrat_10, 0x777B82)),
      _records(std::make_unique<ui::TextLabel>(_content->raw_ptr(), "No tag data",
                                               ui::Frame{14, 145, 292, LV_SIZE_CONTENT}, &lv_font_montserrat_12,
                                               0xE4E4E4)),
      _scrollbar(
          std::make_unique<ui::ScrollBar>(_root->raw_ptr(), ui::Frame{314, kViewportY + 4, 3, kViewportHeight - 8}))
{
    _scroll.springOptions().visualDuration = 0.38F;
    _scroll.springOptions().bounce         = 0.0F;
    _scroll.teleport(0);
    refresh(std::nullopt);
}

void TagDetailPanel::setTagSession(const std::optional<nfc::TagSession>& session)
{
    bool resetScrollForNewTag = false;
    if (session) {
        resetScrollForNewTag = !_session_id || *_session_id != session->sessionId ||
                               _session_technology != session->snapshot.technology ||
                               _session_uid != session->snapshot.uid;
        _session_id         = session->sessionId;
        _session_technology = session->snapshot.technology;
        _session_uid        = session->snapshot.uid;
    } else {
        _session_id.reset();
        _session_technology = nfc::TagTechnology::Unknown;
        _session_uid.clear();
    }

    refresh(session);
    if (resetScrollForNewTag) {
        resetScroll();
    }
}

void TagDetailPanel::scrollBy(int32_t amount)
{
    const float maximum = static_cast<float>(std::max(0, _content_height - kViewportHeight));
    _scroll_target      = std::clamp(_scroll_target - static_cast<float>(amount), 0.0F, maximum);
    _scroll.move(_scroll_target);
}

void TagDetailPanel::setOpacity(lv_opa_t opacity)
{
    if (_applied_opacity == opacity) {
        return;
    }
    _applied_opacity = opacity;
    _root->setOpa(opacity);
}

void TagDetailPanel::setHidden(bool hidden)
{
    if (_hidden == hidden) {
        return;
    }
    _hidden = hidden;
    _root->setHidden(hidden);
}

void TagDetailPanel::setScrollbarHidden(bool hidden)
{
    _scrollbar->setHidden(hidden);
}

void TagDetailPanel::tick(uint32_t nowMs)
{
    if (_scroll.done()) {
        return;
    }
    _scroll.update(static_cast<float>(nowMs) / 1000.0F);
    applyScroll();
}

void TagDetailPanel::refresh(const std::optional<nfc::TagSession>& session)
{
    if (!session) {
        _status->setColor(0x777B82);
        _status->setText("NO TAG");
        _technology->setValue("--");
        _uid->setValue("--");
        _atqa_sak->setValue("--");
        _type->setValue("--");
        _ndef->setValue("--");
        _records->setText("No tag data");
    } else {
        const auto& tag = session->snapshot;
        _status->setColor(session->present ? 0x3FCC75 : 0x777B82);
        _status->setText(session->present ? "PRESENT" : "REMOVED");
        _technology->setValue(nfc::tagTechnologyName(tag.technology));
        _uid->setValue(nfc::bytesToHex(tag.uid));
        _atqa_sak->setValue(atqaSakText(tag));
        _type->setValue(tag.typeName.empty() ? "Unknown" : tag.typeName);
        _ndef->setValue(ndefText(tag));
        _records->setText(recordsText(tag));
    }
    updateContentHeight();
}

void TagDetailPanel::updateContentHeight()
{
    lv_obj_update_layout(_records->raw_ptr());
    _content_height = std::max(kViewportHeight, 145 + lv_obj_get_height(_records->raw_ptr()) + 14);
    _content->setHeight(_content_height);
    const float maximum = static_cast<float>(std::max(0, _content_height - kViewportHeight));
    if (_scroll_target > maximum) {
        _scroll_target = maximum;
        _scroll.move(_scroll_target);
    }
    applyScroll();
}

void TagDetailPanel::resetScroll()
{
    _scroll_target = 0.0F;
    _scroll.teleport(0.0F);
    applyScroll();
}

void TagDetailPanel::applyScroll()
{
    _content->setY(-static_cast<int32_t>(std::lround(_scroll.directValue())));
    _scrollbar->update(_scroll.directValue(), kViewportHeight, _content_height);
}

}  // namespace cap_nfc

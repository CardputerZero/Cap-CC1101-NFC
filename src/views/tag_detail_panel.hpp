#pragma once

#include "nfc/nfc_types.hpp"
#include "views/ui_primitives.hpp"

#include <core/animation/animate_value/animate_value.hpp>
#include <memory>
#include <optional>
#include <vector>

namespace cap_nfc {

class TagDetailPanel {
public:
    explicit TagDetailPanel(lv_obj_t* parent);

    void setTagSession(const std::optional<nfc::TagSession>& session);
    void scrollBy(int32_t amount);
    void setOpacity(lv_opa_t opacity);
    void setHidden(bool hidden);
    void setScrollbarHidden(bool hidden);
    void tick(uint32_t nowMs);

private:
    static constexpr int32_t kViewportY          = 34;
    static constexpr int32_t kViewportHeight     = 111;
    static constexpr int32_t kTypeRowY           = 68;
    static constexpr int32_t kNdefRowY           = 90;
    static constexpr int32_t kDividerY           = 116;
    static constexpr int32_t kRecordsCaptionY    = 126;
    static constexpr int32_t kRecordsY           = 145;
    static constexpr int32_t kDefaultValueHeight = 18;

    std::unique_ptr<ui::Panel> _root;
    std::unique_ptr<ui::TextLabel> _title;
    std::unique_ptr<ui::StatusBadge> _status;
    std::unique_ptr<ui::Panel> _viewport;
    std::unique_ptr<ui::Panel> _content;
    std::unique_ptr<ui::InfoRow> _technology;
    std::unique_ptr<ui::InfoRow> _uid;
    std::unique_ptr<ui::InfoRow> _atqa_sak;
    std::unique_ptr<ui::InfoRow> _type;
    std::unique_ptr<ui::InfoRow> _ndef;
    std::unique_ptr<ui::Panel> _divider;
    std::unique_ptr<ui::TextLabel> _records_caption;
    std::unique_ptr<ui::TextLabel> _records;
    std::unique_ptr<ui::ScrollBar> _scrollbar;
    smooth_ui_toolkit::AnimateValue _scroll{0};
    float _scroll_target    = 0.0F;
    int32_t _content_height = kViewportHeight;
    std::optional<uint64_t> _session_id;
    nfc::TagTechnology _session_technology = nfc::TagTechnology::Unknown;
    std::vector<uint8_t> _session_uid;
    int32_t _applied_opacity = -1;
    bool _hidden             = false;

    void refresh(const std::optional<nfc::TagSession>& session);
    void resetScroll();
    void layoutRows();
    void updateContentHeight();
    void applyScroll();
};

}  // namespace cap_nfc

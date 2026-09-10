#pragma once

#include "nfc/nfc_types.hpp"
#include "views/ui_primitives.hpp"

#include <core/animation/animate_value/animate_value.hpp>
#include <memory>

namespace cap_nfc {

class ReaderInfoPanel {
public:
    explicit ReaderInfoPanel(lv_obj_t* parent);

    void setReaderStatus(const nfc::ReaderStatus& status);
    void scrollBy(int32_t amount);
    void setOpacity(lv_opa_t opacity);
    void setHidden(bool hidden);
    void setScrollbarHidden(bool hidden);
    void tick(uint32_t nowMs);

private:
    static constexpr int32_t kViewportY      = 34;
    static constexpr int32_t kViewportHeight = 111;
    static constexpr int32_t kRowStartY      = 2;
    static constexpr int32_t kRowStep        = 22;
    static constexpr int32_t kRowGap         = 4;

    std::unique_ptr<ui::Panel> _root;
    std::unique_ptr<ui::TextLabel> _title;
    std::unique_ptr<ui::StatusBadge> _status;
    std::unique_ptr<ui::Panel> _viewport;
    std::unique_ptr<ui::Panel> _content;
    std::unique_ptr<ui::InfoRow> _backend;
    std::unique_ptr<ui::InfoRow> _chip;
    std::unique_ptr<ui::InfoRow> _transport;
    std::unique_ptr<ui::InfoRow> _irq;
    std::unique_ptr<ui::InfoRow> _power;
    std::unique_ptr<ui::InfoRow> _protocols;
    std::unique_ptr<ui::Panel> _divider;
    std::unique_ptr<ui::TextLabel> _diagnostics_caption;
    std::unique_ptr<ui::TextLabel> _diagnostics;
    std::unique_ptr<ui::ScrollBar> _scrollbar;
    smooth_ui_toolkit::AnimateValue _scroll{0};
    float _scroll_target     = 0.0F;
    int32_t _content_height  = kViewportHeight;
    int32_t _applied_opacity = -1;
    bool _hidden             = false;

    void layoutRows();
    void updateContentHeight();
    void applyScroll();
};

}  // namespace cap_nfc

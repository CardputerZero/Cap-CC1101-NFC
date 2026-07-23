#pragma once

#include "nfc/nfc_types.hpp"
#include "views/ui_primitives.hpp"

#include <array>
#include <memory>
#include <optional>

namespace cap_nfc {

class ScanPanel {
public:
    explicit ScanPanel(lv_obj_t* parent);

    void setReaderStatus(const nfc::ReaderStatus& status);
    void setTagSession(const std::optional<nfc::TagSession>& session);
    void setOpacity(lv_opa_t opacity);
    void setHidden(bool hidden);
    void tick(uint32_t nowMs);

private:
    std::unique_ptr<ui::Panel> _root;
    std::unique_ptr<ui::TextLabel> _title;
    std::unique_ptr<ui::StatusBadge> _status_badge;
    std::array<std::unique_ptr<ui::Panel>, 3> _rings;
    std::unique_ptr<ui::Panel> _antenna_dot;
    std::unique_ptr<ui::Panel> _divider;
    std::unique_ptr<ui::TextLabel> _technology;
    std::unique_ptr<ui::TextLabel> _uid_caption;
    std::unique_ptr<ui::TextLabel> _uid;
    std::unique_ptr<ui::TextLabel> _type;
    std::unique_ptr<ui::TextLabel> _summary;
    nfc::ReaderStatus _status;
    std::optional<nfc::TagSession> _session;
    int32_t _applied_opacity = -1;
    bool _hidden             = false;

    void refresh();
};

}  // namespace cap_nfc

#pragma once

#include "views/ui_primitives.hpp"

#include <memory>

namespace cap_nfc {

class HelpView {
public:
    explicit HelpView(lv_obj_t* parent);

    void show();
    void hide();
    void toggle();
    bool visible() const;

private:
    std::unique_ptr<ui::Panel> _overlay;
    std::unique_ptr<ui::Panel> _panel;
    std::unique_ptr<ui::TextLabel> _title;
    std::unique_ptr<ui::TextLabel> _body;
    std::unique_ptr<ui::TextLabel> _footer;
};

}  // namespace cap_nfc

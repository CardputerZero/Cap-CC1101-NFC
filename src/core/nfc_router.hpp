#pragma once

#include "core/nfc_types.hpp"

#include <tools/observable/observable.hpp>
#include <vector>

namespace cap_nfc {

class NfcRouter {
public:
    smooth_ui_toolkit::Observable<PageId>& currentPage()
    {
        return _current_page;
    }

    PageId page() const
    {
        return _current_page.get();
    }

    void replace(PageId page);
    void push(PageId page);
    void back();

private:
    smooth_ui_toolkit::Observable<PageId> _current_page{PageId::Nfc};
    std::vector<PageId> _history;
};

}  // namespace cap_nfc

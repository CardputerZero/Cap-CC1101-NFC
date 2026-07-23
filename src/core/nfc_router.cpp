#include "core/nfc_router.hpp"

namespace cap_nfc {

void NfcRouter::replace(PageId page)
{
    if (_current_page.get() != page) {
        _current_page.set(page);
    }
}

void NfcRouter::push(PageId page)
{
    if (_current_page.get() == page) {
        return;
    }
    _history.push_back(_current_page.get());
    _current_page.set(page);
}

void NfcRouter::back()
{
    if (_history.empty()) {
        replace(PageId::Nfc);
        return;
    }

    const PageId previous = _history.back();
    _history.pop_back();
    _current_page.set(previous);
}

}  // namespace cap_nfc

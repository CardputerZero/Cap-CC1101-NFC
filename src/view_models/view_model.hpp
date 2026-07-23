#pragma once

#include "core/nfc_router.hpp"

#include <cstdint>

namespace cap_nfc {

class ViewModel {
public:
    explicit ViewModel(NfcRouter& router) : _router(router)
    {
    }
    virtual ~ViewModel() = default;

    ViewModel(const ViewModel&)            = delete;
    ViewModel& operator=(const ViewModel&) = delete;

    virtual PageId pageId() const = 0;
    virtual void onEnter()
    {
    }
    virtual void onExit()
    {
    }
    virtual void onKey(uint32_t key)
    {
        (void)key;
    }
    virtual void tick(uint32_t nowMs)
    {
        (void)nowMs;
    }

protected:
    NfcRouter& _router;
};

}  // namespace cap_nfc

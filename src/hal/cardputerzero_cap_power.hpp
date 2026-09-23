#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace cap_nfc::hal {

class CardputerZeroCapPower {
public:
    CardputerZeroCapPower();
    ~CardputerZeroCapPower();

    CardputerZeroCapPower(const CardputerZeroCapPower&)            = delete;
    CardputerZeroCapPower& operator=(const CardputerZeroCapPower&) = delete;
    CardputerZeroCapPower(CardputerZeroCapPower&&)                 = delete;
    CardputerZeroCapPower& operator=(CardputerZeroCapPower&&)      = delete;

    bool enable(std::string& error, const std::atomic_bool* cancel = nullptr);
    // Release GPIO ownership without cutting the Cap's shared SPI I/O supply.
    void disable() noexcept;
    bool enabled() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;

    static bool runPinctrl(const std::vector<std::string>& arguments, std::string& error,
                           const std::atomic_bool* cancel);
};

}  // namespace cap_nfc::hal

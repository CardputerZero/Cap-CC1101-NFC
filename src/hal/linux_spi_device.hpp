#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace cap_nfc::hal {

class LinuxSpiDevice {
public:
    struct Config {
        std::string path;
        std::uint32_t speed_hz     = 1'000'000;
        std::uint32_t mode         = 0;
        std::uint8_t bits_per_word = 8;
        bool no_kernel_cs          = true;
    };

    LinuxSpiDevice() = default;
    explicit LinuxSpiDevice(const Config& config);
    ~LinuxSpiDevice();

    LinuxSpiDevice(const LinuxSpiDevice&)            = delete;
    LinuxSpiDevice& operator=(const LinuxSpiDevice&) = delete;
    LinuxSpiDevice(LinuxSpiDevice&&)                 = delete;
    LinuxSpiDevice& operator=(LinuxSpiDevice&&)      = delete;

    void open(const Config& config);
    void close() noexcept;
    bool isOpen() const noexcept;

    void transfer(const std::uint8_t* tx, std::uint8_t* rx, std::size_t size);
    std::uint8_t transfer(std::uint8_t value);
    void write(const std::uint8_t* data, std::size_t size);

    const Config& config() const noexcept;

private:
    int _fd = -1;
    Config _config;
};

}  // namespace cap_nfc::hal

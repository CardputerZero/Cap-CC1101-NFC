#include "hal/linux_spi_device.hpp"

#if !defined(__linux__)
#error "LinuxSpiDevice is only available on Linux"
#endif

#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <linux/spi/spidev.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

#include <spdlog/spdlog.h>

namespace cap_nfc::hal {
namespace {

void throwSystemError(const std::string& action)
{
    throw std::system_error(errno, std::generic_category(), action);
}

void configure(int fd, unsigned long request, void* value, const std::string& name)
{
    if (::ioctl(fd, request, value) < 0) {
        throwSystemError(name);
    }
}

}  // namespace

LinuxSpiDevice::LinuxSpiDevice(const Config& config)
{
    open(config);
}

LinuxSpiDevice::~LinuxSpiDevice()
{
    close();
}

void LinuxSpiDevice::open(const Config& config)
{
    if (config.path.empty()) {
        throw std::invalid_argument("SPI device path must not be empty");
    }
    if (config.speed_hz == 0) {
        throw std::invalid_argument("SPI speed must be greater than zero");
    }
    if (config.bits_per_word == 0) {
        throw std::invalid_argument("SPI bits per word must be greater than zero");
    }

    close();
    const int fd = ::open(config.path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        throwSystemError("open SPI device " + config.path);
    }

    try {
        std::uint32_t mode = config.mode;
        if (config.no_kernel_cs) {
            mode |= SPI_NO_CS;
        }
        auto bits  = config.bits_per_word;
        auto speed = config.speed_hz;
        configure(fd, SPI_IOC_WR_MODE32, &mode, "configure SPI mode on " + config.path);
        configure(fd, SPI_IOC_WR_BITS_PER_WORD, &bits, "configure SPI bits per word on " + config.path);
        configure(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed, "configure SPI speed on " + config.path);

        std::uint32_t actual_mode  = 0;
        std::uint8_t actual_bits   = 0;
        std::uint32_t actual_speed = 0;
        configure(fd, SPI_IOC_RD_MODE32, &actual_mode, "read back SPI mode on " + config.path);
        configure(fd, SPI_IOC_RD_BITS_PER_WORD, &actual_bits, "read back SPI bits per word on " + config.path);
        configure(fd, SPI_IOC_RD_MAX_SPEED_HZ, &actual_speed, "read back SPI speed on " + config.path);

        if (actual_mode != mode || actual_bits != bits || actual_speed != speed) {
            throw std::runtime_error(
                "SPI configuration readback mismatch on " + config.path + " (requested mode=" + std::to_string(mode) +
                ", bits=" + std::to_string(bits) + ", speed=" + std::to_string(speed) +
                "; actual mode=" + std::to_string(actual_mode) + ", bits=" + std::to_string(actual_bits) +
                ", speed=" + std::to_string(actual_speed) + ")");
        }

        _fd     = fd;
        _config = config;
        spdlog::info("NFC SPI: opened {} (mode={}, speed={} Hz, bits={}, kernel CS={})", _config.path, _config.mode,
                     _config.speed_hz, _config.bits_per_word, !_config.no_kernel_cs);
    } catch (...) {
        ::close(fd);
        throw;
    }
}

void LinuxSpiDevice::close() noexcept
{
    if (_fd < 0) {
        return;
    }
    const int fd = _fd;
    _fd          = -1;
    if (::close(fd) < 0) {
        spdlog::warn("NFC SPI: close {} failed: {}", _config.path, std::strerror(errno));
    }
}

bool LinuxSpiDevice::isOpen() const noexcept
{
    return _fd >= 0;
}

void LinuxSpiDevice::transfer(const std::uint8_t* tx, std::uint8_t* rx, std::size_t size)
{
    if (_fd < 0) {
        throw std::logic_error("SPI device is not open");
    }
    if (size == 0) {
        return;
    }
    if (!tx && !rx) {
        throw std::invalid_argument("SPI transfer needs a transmit or receive buffer");
    }
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("SPI transfer exceeds the Linux spidev message size");
    }

    spi_ioc_transfer transfer{};
    transfer.tx_buf        = reinterpret_cast<std::uintptr_t>(tx);
    transfer.rx_buf        = reinterpret_cast<std::uintptr_t>(rx);
    transfer.len           = static_cast<std::uint32_t>(size);
    transfer.speed_hz      = _config.speed_hz;
    transfer.bits_per_word = _config.bits_per_word;
    if (::ioctl(_fd, SPI_IOC_MESSAGE(1), &transfer) < 0) {
        throwSystemError("SPI transfer on " + _config.path);
    }
}

std::uint8_t LinuxSpiDevice::transfer(std::uint8_t value)
{
    std::uint8_t received = 0;
    transfer(&value, &received, 1);
    return received;
}

void LinuxSpiDevice::write(const std::uint8_t* data, std::size_t size)
{
    if (size > 0 && !data) {
        throw std::invalid_argument("SPI write data must not be null");
    }
    transfer(data, nullptr, size);
}

const LinuxSpiDevice::Config& LinuxSpiDevice::config() const noexcept
{
    return _config;
}

}  // namespace cap_nfc::hal

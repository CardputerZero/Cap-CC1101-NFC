#include "hal/cardputerzero_cap_power.hpp"

#if !defined(__linux__)
#error "CardputerZeroCapPower is only available on Linux"
#endif

#include "hal/linux_gpio_line.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>

#include <spdlog/spdlog.h>

extern char** environ;

namespace cap_nfc::hal {
namespace {

constexpr char kLedClassRoot[]          = "/sys/class/leds";
constexpr char kExt5vLedName[]          = "ext_5v_out";
constexpr char kExtUsbGpioFunLedName[]  = "ext_usb_gpio_fun";
constexpr char kGpioChipPath[]          = "/dev/gpiochip0";
constexpr unsigned int kPowerEnableGpio = 26;

std::string ledAttributePath(const char* name, const char* attribute)
{
    return std::string(kLedClassRoot) + "/" + name + "/" + attribute;
}

bool ledClassAvailable(const char* name)
{
    const std::string path = ledAttributePath(name, "brightness");
    return ::access(path.c_str(), F_OK) == 0;
}

int readLedAttribute(const char* name, const char* attribute)
{
    const std::string path = ledAttributePath(name, attribute);
    const int fd           = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "open " + path);
    }

    char buffer[32]{};
    ssize_t size = -1;
    do {
        size = ::read(fd, buffer, sizeof(buffer) - 1);
    } while (size < 0 && errno == EINTR);
    const int read_error   = errno;
    const int close_result = ::close(fd);
    if (size < 0) {
        throw std::system_error(read_error, std::generic_category(), "read " + path);
    }
    if (close_result < 0) {
        spdlog::warn("NFC power: close {} failed: {}", path, std::strerror(errno));
    }
    if (size == 0) {
        throw std::runtime_error("read " + path + " returned no data");
    }

    char* end        = nullptr;
    errno            = 0;
    const long value = std::strtol(buffer, &end, 10);
    if (errno != 0) {
        throw std::system_error(errno, std::generic_category(), "parse " + path);
    }
    if (end == buffer) {
        throw std::runtime_error("invalid integer in " + path);
    }
    return static_cast<int>(value);
}

void writeLedValue(const char* name, bool enabled)
{
    const std::string path = ledAttributePath(name, "brightness");
    const int fd           = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "open " + path + " for writing");
    }

    const char value = enabled ? '1' : '0';
    ssize_t written  = -1;
    do {
        written = ::write(fd, &value, 1);
    } while (written < 0 && errno == EINTR);
    const int write_error  = errno;
    const int close_result = ::close(fd);
    if (written < 0) {
        throw std::system_error(write_error, std::generic_category(), "write " + path);
    }
    if (close_result < 0) {
        spdlog::warn("NFC power: close {} failed: {}", path, std::strerror(errno));
    }
    if (written != 1) {
        throw std::runtime_error("short write to " + path);
    }
}

void logLedClassInterface(const char* name)
{
    const std::string brightness_path = ledAttributePath(name, "brightness");
    if (!ledClassAvailable(name)) {
        spdlog::info("NFC power: LED class interface '{}' is not present", name);
        return;
    }

    try {
        const int brightness     = readLedAttribute(name, "brightness");
        const int max_brightness = readLedAttribute(name, "max_brightness");
        spdlog::info("NFC power: LED class interface '{}' detected (brightness={}/{}, writable={})", name, brightness,
                     max_brightness, ::access(brightness_path.c_str(), W_OK) == 0);
    } catch (const std::exception& exception) {
        spdlog::warn("NFC power: LED class interface '{}' is present but unreadable: {}", name, exception.what());
    }
}

std::string pinctrlCommand(const std::vector<std::string>& arguments)
{
    std::string command = "pinctrl";
    for (const auto& argument : arguments) {
        command += " ";
        command += argument;
    }
    return command;
}

}  // namespace

struct CardputerZeroCapPower::Impl {
    LinuxGpioLine gpio26{kGpioChipPath, kPowerEnableGpio};
    bool gpio26_pinctrl_high         = false;
    bool gpio26_requested            = false;
    int previous_gpio_fun_brightness = 0;
    bool gpio_fun_restore_needed     = false;
    bool enabled                     = false;
};

CardputerZeroCapPower::CardputerZeroCapPower() : _impl(std::make_unique<Impl>())
{
}

CardputerZeroCapPower::~CardputerZeroCapPower()
{
    disable();
}

bool CardputerZeroCapPower::enable(std::string& error, const std::atomic_bool* cancel)
{
    if (_impl->enabled) {
        error.clear();
        return true;
    }

    error.clear();
    logLedClassInterface(kExt5vLedName);
    logLedClassInterface(kExtUsbGpioFunLedName);
    if (!ledClassAvailable(kExt5vLedName)) {
        error = std::string("required LED class power control '") + kExt5vLedName + "' is unavailable at " +
                ledAttributePath(kExt5vLedName, "brightness") +
                "; refusing to request the system-owned legacy GPIO line directly";
        spdlog::error("NFC power: {}", error);
        return false;
    }
    if (cancel && cancel->load(std::memory_order_acquire)) {
        error = "Cap power enable cancelled";
        return false;
    }

    try {
        if (ledClassAvailable(kExtUsbGpioFunLedName)) {
            const std::string selector_path     = ledAttributePath(kExtUsbGpioFunLedName, "brightness");
            _impl->previous_gpio_fun_brightness = readLedAttribute(kExtUsbGpioFunLedName, "brightness");
            if (_impl->previous_gpio_fun_brightness != 0) {
                try {
                    writeLedValue(kExtUsbGpioFunLedName, false);
                    _impl->gpio_fun_restore_needed = true;
                } catch (const std::system_error& exception) {
                    if (exception.code() == std::make_error_code(std::errc::permission_denied)) {
                        throw std::runtime_error(
                            "HAT GPIO/USB selector is read-only for this user; grant write access to " + selector_path +
                            " or run this hardware test as root");
                    }
                    throw;
                }
            }
            const int selector = readLedAttribute(kExtUsbGpioFunLedName, "brightness");
            if (selector != 0) {
                throw std::runtime_error("HAT GPIO/USB selector remained in USB mode after requesting GPIO mode");
            }
            spdlog::info("NFC power: HAT P0/P1 routed to GPIO26/GPIO23 through '{}'", kExtUsbGpioFunLedName);
        } else {
            spdlog::warn("NFC power: HAT GPIO/USB selector '{}' is unavailable; relying on its hardware pull-down",
                         kExtUsbGpioFunLedName);
        }
    } catch (const std::exception& exception) {
        error = exception.what();
        spdlog::error("NFC power: HAT GPIO routing failed: {}", error);
        disable();
        return false;
    }

    // Even partial initialization must leave the shared SPI I/O supply enabled.
    spdlog::info("NFC power: configuring Cap POWER_EN on GPIO26 high");
    _impl->gpio26_pinctrl_high = true;
    if (!runPinctrl({"set", "26", "op", "dh"}, error, cancel)) {
        spdlog::error("NFC power: pin configuration failed: {}", error);
        disable();
        return false;
    }

    try {
        spdlog::debug("NFC power: requesting {} line {} high", kGpioChipPath, kPowerEnableGpio);
        _impl->gpio26.requestOutput(true);
        _impl->gpio26_requested = true;

        const std::string brightness_path = ledAttributePath(kExt5vLedName, "brightness");
        const int previous_ext5v_brightness = readLedAttribute(kExt5vLedName, "brightness");
        if (previous_ext5v_brightness <= 0) {
            spdlog::debug("NFC power: enabling EXT5V through {}", brightness_path);
            try {
                writeLedValue(kExt5vLedName, true);
            } catch (const std::system_error& exception) {
                if (exception.code() == std::make_error_code(std::errc::permission_denied)) {
                    throw std::runtime_error("EXT5V LED class is read-only for this user; grant write access to " +
                                             brightness_path +
                                             ", pre-enable it as root, or run this hardware test as root");
                }
                throw;
            }
        } else {
            spdlog::info("NFC power: EXT5V was already enabled; leaving system-owned state unchanged");
        }

        try {
            const int brightness = readLedAttribute(kExt5vLedName, "brightness");
            if (brightness > 0) {
                spdlog::info("NFC power: EXT5V enabled through LED class (brightness={})", brightness);
            } else {
                spdlog::warn(
                    "NFC power: EXT5V enable write succeeded but LED class still reports brightness=0; "
                    "continuing to the ST25R3916 hardware probe");
            }
        } catch (const std::exception& exception) {
            spdlog::warn(
                "NFC power: EXT5V enable write succeeded but readback failed: {}; continuing to the ST25R3916 "
                "hardware probe",
                exception.what());
        }

        if (cancel && cancel->load(std::memory_order_acquire)) {
            throw std::runtime_error("Cap power enable cancelled");
        }

        _impl->enabled = true;
        spdlog::info("NFC power: Cap power controls configured (gpiochip0:26=1, EXT5V requested on)");
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        spdlog::error("NFC power: enable failed: {}", error);
        disable();
        return false;
    }
}

void CardputerZeroCapPower::disable() noexcept
{
    // GPIO26 gates the Cap's 3.3 V rail, including the CC1101 SPI pins. Driving
    // it low can freeze the LCD even while framebuffer/SPI writes succeed.
    // Releasing a GPIO request retains its output level on this BSP.
    if (_impl->gpio26_requested) {
        _impl->gpio26.release();
        _impl->gpio26_requested = false;
    }
    if (_impl->gpio26_pinctrl_high) {
        spdlog::info("NFC power: retaining GPIO26 POWER_EN and EXT5V for the shared LCD SPI bus");
    }

    _impl->gpio26_pinctrl_high = false;

    if (_impl->gpio_fun_restore_needed) {
        try {
            writeLedValue(kExtUsbGpioFunLedName, _impl->previous_gpio_fun_brightness != 0);
            spdlog::debug("NFC power: restored HAT GPIO/USB selector brightness to {}",
                          _impl->previous_gpio_fun_brightness);
        } catch (const std::exception& exception) {
            spdlog::warn("NFC power: failed to restore HAT GPIO/USB selector: {}", exception.what());
        }
    }
    _impl->gpio_fun_restore_needed = false;
    _impl->enabled                 = false;
}

bool CardputerZeroCapPower::enabled() const noexcept
{
    return _impl->enabled;
}

bool CardputerZeroCapPower::runPinctrl(const std::vector<std::string>& arguments, std::string& error,
                                       const std::atomic_bool* cancel)
{
    const std::string command = pinctrlCommand(arguments);
    const auto started_at     = std::chrono::steady_clock::now();
    spdlog::debug("NFC power: running `{}`", command);

    std::vector<std::string> storage;
    storage.reserve(arguments.size() + 1);
    storage.emplace_back("pinctrl");
    storage.insert(storage.end(), arguments.begin(), arguments.end());

    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (auto& value : storage) {
        argv.push_back(value.data());
    }
    argv.push_back(nullptr);

    pid_t child            = -1;
    const int spawn_result = ::posix_spawnp(&child, "pinctrl", nullptr, nullptr, argv.data(), environ);
    if (spawn_result != 0) {
        error = command + " spawn failed: " + std::strerror(spawn_result);
        return false;
    }

    int status          = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (true) {
        const pid_t wait_result = ::waitpid(child, &status, WNOHANG);
        if (wait_result == child) {
            break;
        }
        if (wait_result < 0 && errno != EINTR) {
            const int wait_error = errno;
            (void)::kill(child, SIGKILL);
            (void)::waitpid(child, &status, 0);
            error = command + " wait failed: " + std::strerror(wait_error);
            return false;
        }
        if (cancel && cancel->load(std::memory_order_acquire)) {
            (void)::kill(child, SIGKILL);
            (void)::waitpid(child, &status, 0);
            error = command + " cancelled";
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            (void)::kill(child, SIGKILL);
            (void)::waitpid(child, &status, 0);
            error = command + " timed out after 2000 ms";
            return false;
        }
        if (wait_result < 0 && errno == EINTR) {
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (WIFSIGNALED(status)) {
            error = command + " terminated by signal " + std::to_string(WTERMSIG(status));
        } else {
            error = command + " exited with status " + std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        }
        return false;
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at).count();
    spdlog::debug("NFC power: `{}` completed in {} ms", command, elapsed);
    return true;
}

}  // namespace cap_nfc::hal

#include "hal/cap_spi_overlay.hpp"

#if !defined(__linux__)
#error "The Cap SPI overlay loader is only available on Linux"
#endif

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/file.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <spdlog/spdlog.h>

extern char** environ;

namespace cap_nfc::hal {
namespace {

using Clock = std::chrono::steady_clock;

constexpr char kOverlayDirectory[] = "/boot/firmware/overlays";
constexpr char kOverlayName[]      = "spi0-spidev2-gpio22-overlay";
constexpr char kOverlayPath[]      = "/boot/firmware/overlays/spi0-spidev2-gpio22-overlay.dtbo";
constexpr char kDtoverlayPath[]    = "/usr/bin/dtoverlay";
constexpr char kTimeoutPath[]      = "/usr/bin/timeout";
constexpr char kLockPath[]         = "/run/lock/cardputerzero-cap-spi-overlay.lock";

bool cancelled(const std::atomic_bool* cancel)
{
    return cancel && cancel->load(std::memory_order_acquire);
}

bool deviceAvailable(const std::string& path)
{
    return ::access(path.c_str(), F_OK) == 0;
}

void reapInBackground(pid_t child) noexcept
{
    try {
        std::thread([child] {
            int status = 0;
            while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
        }).detach();
    } catch (...) {
    }
}

void stopChildWithoutBlocking(pid_t child) noexcept
{
    if (::kill(-child, SIGKILL) < 0) {
        (void)::kill(child, SIGKILL);
    }
    int status             = 0;
    const pid_t waitResult = ::waitpid(child, &status, WNOHANG);
    if (waitResult == 0 || (waitResult < 0 && errno == EINTR)) {
        reapInBackground(child);
    }
}

std::string commandText()
{
    return std::string(kTimeoutPath) + " --signal=KILL 5s " + kDtoverlayPath + " -d " + kOverlayDirectory + " " +
           kOverlayName;
}

class OverlayLoadLock {
public:
    ~OverlayLoadLock()
    {
        if (_fd >= 0) {
            (void)::flock(_fd, LOCK_UN);
            (void)::close(_fd);
        }
    }

    bool acquire(std::string& error, const std::atomic_bool* cancel)
    {
        _fd = ::open(kLockPath, O_RDONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0666);
        if (_fd < 0) {
            error = std::string("open SPI overlay lock ") + kLockPath + ": " + std::strerror(errno);
            return false;
        }

        const auto deadline = Clock::now() + std::chrono::seconds(8);
        while (::flock(_fd, LOCK_EX | LOCK_NB) < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                error = std::string("lock SPI overlay state ") + kLockPath + ": " + std::strerror(errno);
                return false;
            }
            if (cancelled(cancel)) {
                error = "SPI overlay lock wait cancelled";
                return false;
            }
            if (Clock::now() >= deadline) {
                error = "timed out waiting 8000 ms for the SPI overlay lock";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return true;
    }

private:
    int _fd = -1;
};

bool requireRoot(std::string& error)
{
    if (::geteuid() == 0) {
        return true;
    }
    error =
        "runtime SPI overlay loading requires root; launch the installed app through APPLaunch or run it with "
        "sudo";
    return false;
}

bool runOverlayLoader(std::string& error, const std::atomic_bool* cancel)
{
    if (!requireRoot(error)) {
        return false;
    }
    std::vector<std::string> arguments = {kTimeoutPath, "--signal=KILL",   "5s",        kDtoverlayPath,
                                          "-d",         kOverlayDirectory, kOverlayName};

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    posix_spawnattr_t attributes;
    int result = ::posix_spawnattr_init(&attributes);
    if (result != 0) {
        error = "initialize dtoverlay process attributes: " + std::string(std::strerror(result));
        return false;
    }

    result = ::posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    if (result == 0) {
        result = ::posix_spawnattr_setpgroup(&attributes, 0);
    }
    if (result != 0) {
        ::posix_spawnattr_destroy(&attributes);
        error = "configure dtoverlay process group: " + std::string(std::strerror(result));
        return false;
    }

    const std::string command = commandText();
    spdlog::info("NFC SPI overlay: running `{}`", command);

    pid_t child = -1;
    result      = ::posix_spawn(&child, arguments.front().c_str(), nullptr, &attributes, argv.data(), environ);
    ::posix_spawnattr_destroy(&attributes);
    if (result != 0) {
        error = command + " spawn failed: " + std::string(std::strerror(result));
        return false;
    }

    int status          = 0;
    const auto deadline = Clock::now() + std::chrono::seconds(6);
    while (true) {
        const pid_t waitResult = ::waitpid(child, &status, WNOHANG);
        if (waitResult == child) {
            break;
        }
        if (waitResult < 0 && errno != EINTR) {
            const int waitError = errno;
            stopChildWithoutBlocking(child);
            error = command + " wait failed: " + std::string(std::strerror(waitError));
            return false;
        }
        if (cancelled(cancel)) {
            stopChildWithoutBlocking(child);
            error = command + " cancelled";
            return false;
        }
        if (Clock::now() >= deadline) {
            stopChildWithoutBlocking(child);
            error = command + " did not exit within 6000 ms";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        spdlog::info("NFC SPI overlay: dtoverlay completed successfully");
        return true;
    }
    if (WIFSIGNALED(status)) {
        error = command + " terminated by signal " + std::to_string(WTERMSIG(status));
    } else {
        error = command + " exited with status " + std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    }
    return false;
}

}  // namespace

bool ensureCapSpiOverlay(const std::string& expectedDevice, std::string& error, const std::atomic_bool* cancel)
{
    error.clear();
    if (expectedDevice.empty()) {
        error = "expected SPI device path is empty";
        return false;
    }
    if (deviceAvailable(expectedDevice)) {
        spdlog::info("NFC SPI overlay: {} is already available; runtime load is not needed", expectedDevice);
        return true;
    }
    if (cancelled(cancel)) {
        error = "SPI overlay load cancelled";
        return false;
    }
    if (::access(kOverlayPath, F_OK) != 0) {
        error = std::string("required BSP overlay is missing: ") + kOverlayPath + ": " + std::strerror(errno);
        return false;
    }

    OverlayLoadLock lock;
    if (!lock.acquire(error, cancel)) {
        return false;
    }
    if (deviceAvailable(expectedDevice)) {
        spdlog::info("NFC SPI overlay: {} became available while waiting for the loader lock", expectedDevice);
        return true;
    }

    spdlog::info("NFC SPI overlay: {} is missing; requesting runtime overlay {}", expectedDevice, kOverlayPath);
    std::string loaderError;
    const bool loaderSucceeded = runOverlayLoader(loaderError, cancel);

    const auto deadline = Clock::now() + std::chrono::seconds(2);
    while (!deviceAvailable(expectedDevice) && Clock::now() < deadline) {
        if (cancelled(cancel)) {
            error = "SPI overlay device wait cancelled";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (deviceAvailable(expectedDevice)) {
        if (loaderSucceeded) {
            spdlog::info("NFC SPI overlay: device {} is ready", expectedDevice);
        } else {
            spdlog::warn("NFC SPI overlay: loader reported `{}`, but {} appeared; continuing", loaderError,
                         expectedDevice);
        }
        return true;
    }

    if (loaderSucceeded) {
        error = "dtoverlay completed, but expected SPI device " + expectedDevice + " did not appear within 2000 ms";
    } else {
        error = loaderError + "; expected SPI device " + expectedDevice + " is still missing";
    }
    return false;
}

}  // namespace cap_nfc::hal

#include "hal/linux_gpio_line.hpp"

#if !defined(__linux__)
#error "LinuxGpioLine is only available on Linux"
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <gpiod.h>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

namespace cap_nfc::hal {
namespace {

constexpr int kCancellationPollMs = 50;

std::runtime_error gpioError(const std::string& action)
{
    const int error = errno;
    return std::runtime_error(action + " failed: " + std::strerror(error));
}

#if defined(GPIOD_V2)
class LineSettings {
public:
    LineSettings() : value(gpiod_line_settings_new())
    {
    }
    ~LineSettings()
    {
        gpiod_line_settings_free(value);
    }
    gpiod_line_settings* value;
};

class LineConfig {
public:
    LineConfig() : value(gpiod_line_config_new())
    {
    }
    ~LineConfig()
    {
        gpiod_line_config_free(value);
    }
    gpiod_line_config* value;
};

class RequestConfig {
public:
    RequestConfig() : value(gpiod_request_config_new())
    {
    }
    ~RequestConfig()
    {
        gpiod_request_config_free(value);
    }
    gpiod_request_config* value;
};

class EdgeEventBuffer {
public:
    EdgeEventBuffer() : value(gpiod_edge_event_buffer_new(1))
    {
    }
    ~EdgeEventBuffer()
    {
        gpiod_edge_event_buffer_free(value);
    }
    gpiod_edge_event_buffer* value;
};
#endif

}  // namespace

LinuxGpioLine::LinuxGpioLine(std::string chip_path, unsigned int offset, std::string consumer)
    : _chip_path(std::move(chip_path)), _offset(offset), _consumer(std::move(consumer))
{
    if (_chip_path.empty()) {
        throw std::invalid_argument("GPIO chip path must not be empty");
    }
    if (_consumer.empty()) {
        throw std::invalid_argument("GPIO consumer must not be empty");
    }
}

LinuxGpioLine::~LinuxGpioLine()
{
    release();
    if (_chip) {
        gpiod_chip_close(_chip);
        _chip = nullptr;
    }
}

void LinuxGpioLine::ensureChip()
{
    if (_chip) {
        return;
    }

    _chip = gpiod_chip_open(_chip_path.c_str());
    if (!_chip) {
        throw gpioError("open GPIO chip " + _chip_path);
    }
#if !defined(GPIOD_V2)
    _line = gpiod_chip_get_line(_chip, _offset);
    if (!_line) {
        const auto exception = gpioError("get GPIO line " + _chip_path + ":" + std::to_string(_offset));
        gpiod_chip_close(_chip);
        _chip = nullptr;
        throw exception;
    }
#endif
}

void LinuxGpioLine::request(RequestMode mode, bool output_value)
{
    if (mode == RequestMode::None) {
        release();
        return;
    }

    ensureChip();
    release();

#if defined(GPIOD_V2)
    LineSettings settings;
    LineConfig line_config;
    RequestConfig request_config;
    if (!settings.value || !line_config.value || !request_config.value) {
        throw gpioError("allocate libgpiod request objects");
    }

    int result = gpiod_line_settings_set_direction(
        settings.value, mode == RequestMode::Output ? GPIOD_LINE_DIRECTION_OUTPUT : GPIOD_LINE_DIRECTION_INPUT);
    if (result == 0 && mode == RequestMode::Output) {
        result = gpiod_line_settings_set_output_value(
            settings.value, output_value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
    }
    if (result == 0 && mode == RequestMode::RisingEdge) {
        result = gpiod_line_settings_set_edge_detection(settings.value, GPIOD_LINE_EDGE_RISING);
    }
    if (result == 0) {
        result = gpiod_line_config_add_line_settings(line_config.value, &_offset, 1, settings.value);
    }
    if (result < 0) {
        throw gpioError("configure " + std::string(requestModeName(mode)) + " GPIO " + _chip_path + ":" +
                        std::to_string(_offset));
    }

    gpiod_request_config_set_consumer(request_config.value, _consumer.c_str());
    _request = gpiod_chip_request_lines(_chip, request_config.value, line_config.value);
    if (!_request) {
        throw gpioError("request " + std::string(requestModeName(mode)) + " GPIO " + _chip_path + ":" +
                        std::to_string(_offset));
    }
#else
    int result = -1;
    switch (mode) {
        case RequestMode::Input:
            result = gpiod_line_request_input(_line, _consumer.c_str());
            break;
        case RequestMode::Output:
            result = gpiod_line_request_output(_line, _consumer.c_str(), output_value ? 1 : 0);
            break;
        case RequestMode::RisingEdge:
            result = gpiod_line_request_rising_edge_events(_line, _consumer.c_str());
            break;
        case RequestMode::None:
            break;
    }
    if (result < 0) {
        throw gpioError("request " + std::string(requestModeName(mode)) + " GPIO " + _chip_path + ":" +
                        std::to_string(_offset));
    }
#endif

    _request_mode = mode;
    _output_value = output_value;
    spdlog::debug("NFC GPIO: requested {}:{} as {} (consumer={})", _chip_path, _offset, requestModeName(mode),
                  _consumer);
}

void LinuxGpioLine::requestInput()
{
    request(RequestMode::Input);
}

void LinuxGpioLine::requestOutput(bool initial_value)
{
    request(RequestMode::Output, initial_value);
}

void LinuxGpioLine::requestRisingEdge()
{
    request(RequestMode::RisingEdge);
}

void LinuxGpioLine::release() noexcept
{
#if defined(GPIOD_V2)
    if (_request) {
        gpiod_line_request_release(_request);
        _request = nullptr;
    }
#else
    if (_request_mode != RequestMode::None && _line) {
        gpiod_line_release(_line);
    }
#endif
    _request_mode = RequestMode::None;
}

void LinuxGpioLine::setValue(bool value)
{
    if (_request_mode == RequestMode::None) {
        requestOutput(value);
        return;
    }
    if (_request_mode != RequestMode::Output) {
        throw std::logic_error("cannot write non-output GPIO " + _chip_path + ":" + std::to_string(_offset));
    }

#if defined(GPIOD_V2)
    if (gpiod_line_request_set_value(_request, _offset, value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE) <
        0) {
        throw gpioError("write GPIO " + _chip_path + ":" + std::to_string(_offset));
    }
#else
    if (gpiod_line_set_value(_line, value ? 1 : 0) < 0) {
        throw gpioError("write GPIO " + _chip_path + ":" + std::to_string(_offset));
    }
#endif
    _output_value = value;
}

bool LinuxGpioLine::value() const
{
    if (_request_mode == RequestMode::None) {
        throw std::logic_error("cannot read unrequested GPIO " + _chip_path + ":" + std::to_string(_offset));
    }

#if defined(GPIOD_V2)
    const gpiod_line_value result = gpiod_line_request_get_value(_request, _offset);
    if (result == GPIOD_LINE_VALUE_ERROR) {
        throw gpioError("read GPIO " + _chip_path + ":" + std::to_string(_offset));
    }
    return result == GPIOD_LINE_VALUE_ACTIVE;
#else
    const int result = gpiod_line_get_value(_line);
    if (result < 0) {
        throw gpioError("read GPIO " + _chip_path + ":" + std::to_string(_offset));
    }
    return result != 0;
#endif
}

bool LinuxGpioLine::waitOne(int timeout_ms)
{
#if defined(GPIOD_V2)
    const int64_t timeout_ns = timeout_ms < 0 ? -1 : static_cast<int64_t>(timeout_ms) * 1'000'000LL;
    int result               = gpiod_line_request_wait_edge_events(_request, timeout_ns);
    if (result < 0) {
        throw gpioError("wait for rising edge on GPIO " + _chip_path + ":" + std::to_string(_offset));
    }
    if (result == 0) {
        return false;
    }

    EdgeEventBuffer events;
    if (!events.value) {
        throw gpioError("allocate GPIO edge event buffer");
    }
    result = gpiod_line_request_read_edge_events(_request, events.value, 1);
    if (result < 0) {
        throw gpioError("read rising edge on GPIO " + _chip_path + ":" + std::to_string(_offset));
    }
    return result > 0;
#else
    timespec timeout{};
    timespec* timeout_ptr = nullptr;
    if (timeout_ms >= 0) {
        timeout.tv_sec  = timeout_ms / 1000;
        timeout.tv_nsec = static_cast<long>(timeout_ms % 1000) * 1'000'000L;
        timeout_ptr     = &timeout;
    }
    int result = gpiod_line_event_wait(_line, timeout_ptr);
    if (result < 0) {
        throw gpioError("wait for rising edge on GPIO " + _chip_path + ":" + std::to_string(_offset));
    }
    if (result == 0) {
        return false;
    }

    gpiod_line_event event{};
    if (gpiod_line_event_read(_line, &event) < 0) {
        throw gpioError("read rising edge on GPIO " + _chip_path + ":" + std::to_string(_offset));
    }
    return true;
#endif
}

LinuxGpioLine::WaitResult LinuxGpioLine::waitForRisingEdge(int timeout_ms, const std::atomic_bool* cancel)
{
    if (_request_mode != RequestMode::RisingEdge) {
        throw std::logic_error("GPIO " + _chip_path + ":" + std::to_string(_offset) +
                               " must be requested for rising-edge events before waiting");
    }
    if (cancel && cancel->load(std::memory_order_acquire)) {
        return WaitResult::Cancelled;
    }

    const bool infinite_wait = timeout_ms < 0;
    const auto deadline      = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(timeout_ms, 0));
    while (true) {
        int wait_ms = timeout_ms;
        if (cancel) {
            if (infinite_wait) {
                wait_ms = kCancellationPollMs;
            } else {
                const auto remaining =
                    std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                        .count();
                wait_ms = static_cast<int>(std::clamp<long long>(remaining, 0, kCancellationPollMs));
            }
        }

        if (waitOne(wait_ms)) {
            return WaitResult::RisingEdge;
        }
        if (cancel && cancel->load(std::memory_order_acquire)) {
            return WaitResult::Cancelled;
        }
        if (!infinite_wait && std::chrono::steady_clock::now() >= deadline) {
            return WaitResult::Timeout;
        }
        if (!cancel) {
            return WaitResult::Timeout;
        }
    }
}

const std::string& LinuxGpioLine::chipPath() const noexcept
{
    return _chip_path;
}

unsigned int LinuxGpioLine::offset() const noexcept
{
    return _offset;
}

LinuxGpioLine::RequestMode LinuxGpioLine::requestMode() const noexcept
{
    return _request_mode;
}

bool LinuxGpioLine::requested() const noexcept
{
    return _request_mode != RequestMode::None;
}

const char* LinuxGpioLine::requestModeName(RequestMode mode) noexcept
{
    switch (mode) {
        case RequestMode::None:
            return "unrequested";
        case RequestMode::Input:
            return "input";
        case RequestMode::Output:
            return "output";
        case RequestMode::RisingEdge:
            return "rising-edge input";
    }
    return "unknown";
}

}  // namespace cap_nfc::hal

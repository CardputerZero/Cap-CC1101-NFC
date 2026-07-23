#pragma once

#include <atomic>
#include <string>

struct gpiod_chip;
#if defined(GPIOD_V2)
struct gpiod_line_request;
#else
struct gpiod_line;
#endif

namespace cap_nfc::hal {

class LinuxGpioLine {
public:
    enum class RequestMode { None, Input, Output, RisingEdge };
    enum class WaitResult { RisingEdge, Timeout, Cancelled };

    LinuxGpioLine(std::string chip_path, unsigned int offset, std::string consumer = "cap-cc1101-nfc");
    ~LinuxGpioLine();

    LinuxGpioLine(const LinuxGpioLine&)            = delete;
    LinuxGpioLine& operator=(const LinuxGpioLine&) = delete;
    LinuxGpioLine(LinuxGpioLine&&)                 = delete;
    LinuxGpioLine& operator=(LinuxGpioLine&&)      = delete;

    void requestInput();
    void requestOutput(bool initial_value);
    void requestRisingEdge();
    void release() noexcept;

    void setValue(bool value);
    bool value() const;
    WaitResult waitForRisingEdge(int timeout_ms, const std::atomic_bool* cancel = nullptr);

    const std::string& chipPath() const noexcept;
    unsigned int offset() const noexcept;
    RequestMode requestMode() const noexcept;
    bool requested() const noexcept;

private:
    std::string _chip_path;
    unsigned int _offset;
    std::string _consumer;
    gpiod_chip* _chip = nullptr;
#if defined(GPIOD_V2)
    gpiod_line_request* _request = nullptr;
#else
    gpiod_line* _line = nullptr;
#endif
    RequestMode _request_mode = RequestMode::None;
    bool _output_value        = false;

    void ensureChip();
    void request(RequestMode mode, bool output_value = false);
    bool waitOne(int timeout_ms);
    static const char* requestModeName(RequestMode mode) noexcept;
};

}  // namespace cap_nfc::hal

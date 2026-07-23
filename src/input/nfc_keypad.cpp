#include "input/nfc_keypad.hpp"

#include <spdlog/spdlog.h>
#include <cstdio>
#include <cstdlib>
#include <utility>

#if !LV_USE_SDL && defined(__linux__)
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace cap_nfc {

#if !LV_USE_SDL && defined(__linux__)
namespace {

template <size_t N>
bool testBit(const std::array<unsigned long, N>& bits, unsigned int bit)
{
    constexpr unsigned int bits_per_word = sizeof(unsigned long) * 8;
    const unsigned int index             = bit / bits_per_word;
    const unsigned int offset            = bit % bits_per_word;
    return index < bits.size() && ((bits[index] >> offset) & 1UL) != 0;
}

bool hasAppKeys(int fd)
{
    constexpr size_t key_bits_size = (KEY_MAX + sizeof(unsigned long) * 8) / (sizeof(unsigned long) * 8);
    std::array<unsigned long, key_bits_size> key_bits{};
    if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits.data()) < 0) {
        return false;
    }

    return testBit(key_bits, KEY_ESC) || testBit(key_bits, KEY_ENTER) || testBit(key_bits, KEY_KPENTER) ||
           testBit(key_bits, KEY_UP) || testBit(key_bits, KEY_DOWN) || testBit(key_bits, KEY_LEFT) ||
           testBit(key_bits, KEY_RIGHT) || testBit(key_bits, KEY_F) || testBit(key_bits, KEY_X) ||
           testBit(key_bits, KEY_Z) || testBit(key_bits, KEY_C) || testBit(key_bits, KEY_SPACE) ||
           testBit(key_bits, KEY_A) || testBit(key_bits, KEY_BACKSPACE) || testBit(key_bits, KEY_0) ||
           testBit(key_bits, KEY_TAB) || testBit(key_bits, KEY_1) || testBit(key_bits, KEY_2) ||
           testBit(key_bits, KEY_3) || testBit(key_bits, KEY_4) || testBit(key_bits, KEY_5) ||
           testBit(key_bits, KEY_6) || testBit(key_bits, KEY_7) || testBit(key_bits, KEY_8) || testBit(key_bits, KEY_9);
}

bool envEnabled(const char* name, bool fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return fallback;
    }

    return std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0 && std::strcmp(value, "False") != 0 &&
           std::strcmp(value, "off") != 0 && std::strcmp(value, "OFF") != 0;
}

bool inputDebugEnabled()
{
    static const bool enabled = envEnabled("CAP_NFC_KEYBOARD_DEBUG", false);
    return enabled;
}

const char* debugKeyUtf8(uint32_t key)
{
    static char text[8] = {0};
    if (key >= 0x20 && key < 0x7f) {
        text[0] = '\'';
        text[1] = static_cast<char>(key);
        text[2] = '\'';
        text[3] = '\0';
        return text;
    }
    std::snprintf(text, sizeof(text), "none");
    return text;
}

struct Tca8418KeymapEntry {
    uint16_t code;
    char text;
};

constexpr Tca8418KeymapEntry kTca8418Keymap[] = {
    {183, '!'}, {184, '@'}, {185, '#'}, {186, '$'},  {187, '%'}, {188, '^'}, {189, '&'}, {190, '*'},
    {191, '('}, {192, ')'}, {193, '~'}, {194, '`'},  {195, '+'}, {196, '-'}, {197, '/'}, {198, '\\'},
    {199, '{'}, {200, '}'}, {201, '['}, {202, ']'},  {209, '='}, {210, ':'}, {211, ';'}, {212, '_'},
    {213, '?'}, {214, '<'}, {215, '>'}, {216, '\''}, {217, '"'}, {231, ','}, {232, '.'}, {233, '|'},
};

uint32_t tca8418Utf8(uint16_t code)
{
    for (const auto& entry : kTca8418Keymap) {
        if (entry.code == code) {
            return static_cast<uint8_t>(entry.text);
        }
    }
    return 0;
}

}  // namespace
#endif

NfcKeypad::~NfcKeypad()
{
    close();
}

bool NfcKeypad::openDefault()
{
#if !LV_USE_SDL && defined(__linux__)
    if (const char* device_path = std::getenv("CAP_NFC_KEYBOARD_DEVICE")) {
        return openDevice(device_path, false);
    }
    if (const char* device_path = std::getenv("APPLAUNCH_LINUX_KEYBOARD_DEVICE")) {
        return openDevice(device_path, false);
    }
    if (const char* device_path = std::getenv("LV_LINUX_KEYBOARD_DEVICE")) {
        return openDevice(device_path, false);
    }

    if (openDevice("/dev/input/by-path/platform-3f804000.i2c-event", false)) {
        return true;
    }

    bool opened = false;
    for (int index = 0; index < 32; ++index) {
        opened = openDevice("/dev/input/event" + std::to_string(index), true) || opened;
    }

    if (!opened) {
        spdlog::warn("NfcKeypad: no keyboard input device opened");
    }
    return opened;
#else
    return false;
#endif
}

bool NfcKeypad::openDevice(const std::string& path, bool require_app_keys)
{
#if !LV_USE_SDL && defined(__linux__)
    if (!ensureIndev()) {
        return false;
    }

    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    if (require_app_keys && !hasAppKeys(fd)) {
        ::close(fd);
        return false;
    }

    const bool grab_input = envEnabled("CAP_NFC_KEYBOARD_GRAB", false);
    if (grab_input && ::ioctl(fd, EVIOCGRAB, 1) < 0) {
        spdlog::warn("NfcKeypad: failed to grab {}: {}", path, std::strerror(errno));
        ::close(fd);
        return false;
    }

    _event_fds.push_back(fd);
    spdlog::info("NfcKeypad: opened {}{}", path, grab_input ? " with grab" : "");
    return true;
#else
    (void)path;
    (void)require_app_keys;
    return false;
#endif
}

void NfcKeypad::close()
{
#if !LV_USE_SDL && defined(__linux__)
    for (int fd : _event_fds) {
        if (fd >= 0) {
            (void)::ioctl(fd, EVIOCGRAB, 0);
            ::close(fd);
        }
    }
#endif
    _event_fds.clear();
    _pending_keys.clear();

    if (_indev) {
        lv_indev_delete(_indev);
        _indev = nullptr;
    }
}

void NfcKeypad::poll()
{
#if !LV_USE_SDL && defined(__linux__)
    for (int fd : _event_fds) {
        while (true) {
            input_event event{};
            const ssize_t bytes_read = ::read(fd, &event, sizeof(event));
            if (bytes_read == sizeof(event)) {
                if (event.type == EV_KEY) {
                    pushKeyEvent(event.code, event.value);
                }
                continue;
            }

            if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                break;
            }

            if (bytes_read < 0) {
                spdlog::warn("NfcKeypad: failed to read input event: {}", std::strerror(errno));
            }
            break;
        }
    }
#endif
}

lv_indev_t* NfcKeypad::indev() const
{
    return _indev;
}

void NfcKeypad::setKeyCallback(KeyCallback callback)
{
    _key_callback = std::move(callback);
}

void NfcKeypad::readCb(lv_indev_t* indev, lv_indev_data_t* data)
{
    auto* keypad = static_cast<NfcKeypad*>(lv_indev_get_user_data(indev));
    if (!keypad || !data) {
        return;
    }

    if (!keypad->_pending_keys.empty()) {
        const KeyEvent event = keypad->_pending_keys.front();
        keypad->_pending_keys.pop_front();
        keypad->_last_key      = event.key;
        data->key              = event.key;
        data->state            = event.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->continue_reading = !keypad->_pending_keys.empty();
        return;
    }

    data->key   = keypad->_last_key;
    data->state = LV_INDEV_STATE_RELEASED;
}

bool NfcKeypad::ensureIndev()
{
    if (_indev) {
        return true;
    }

    _indev = lv_indev_create();
    if (!_indev) {
        spdlog::error("NfcKeypad: failed to create LVGL keypad indev");
        return false;
    }

    lv_indev_set_type(_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(_indev, readCb);
    lv_indev_set_user_data(_indev, this);
    return true;
}

void NfcKeypad::pushKeyEvent(uint16_t code, int32_t value)
{
    if (value != 0 && value != 1) {
#if !LV_USE_SDL && defined(__linux__)
        if (inputDebugEnabled()) {
            spdlog::info("NfcKeypad: ignored repeat key code={}, value={}", code, value);
        }
#endif
        return;
    }

#if !LV_USE_SDL && defined(__linux__)
    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
        if (code == KEY_LEFTSHIFT) {
            _left_shift_pressed = value == 1;
        } else {
            _right_shift_pressed = value == 1;
        }
        return;
    }
#endif

    const uint32_t key = translateKey(code);
    if (key == 0) {
#if !LV_USE_SDL && defined(__linux__)
        if (inputDebugEnabled()) {
            spdlog::info("NfcKeypad: ignored key code={}, value={}", code, value);
        }
#endif
        return;
    }

    const bool pressed = value == 1;
#if !LV_USE_SDL && defined(__linux__)
    if (inputDebugEnabled()) {
        spdlog::info("NfcKeypad: key code={}, translated={}, utf8={}, pressed={}", code, key, debugKeyUtf8(key),
                     pressed);
    }
#endif
    bool consumed = false;
    if (_key_callback) {
        consumed = _key_callback(key, keyUtf8(key), pressed);
    }

    if (consumed) {
        return;
    }

    _pending_keys.push_back({key, pressed});
}

uint32_t NfcKeypad::translateKey(uint16_t code) const
{
#if !LV_USE_SDL && defined(__linux__)
    const bool shifted = shiftPressed();
    if (const uint32_t tca8418_key = tca8418Utf8(code)) {
        return tca8418_key;
    }

    switch (code) {
        case KEY_ESC:
            return LV_KEY_ESC;
        case KEY_ENTER:
        case KEY_KPENTER:
            return LV_KEY_ENTER;
        case KEY_BACKSPACE:
            return LV_KEY_BACKSPACE;
        case KEY_DELETE:
            return LV_KEY_DEL;
        case KEY_HOME:
            return LV_KEY_HOME;
        case KEY_END:
            return LV_KEY_END;
        case KEY_UP:
            return LV_KEY_UP;
        case KEY_DOWN:
            return LV_KEY_DOWN;
        case KEY_LEFT:
            return LV_KEY_LEFT;
        case KEY_RIGHT:
            return LV_KEY_RIGHT;
        case KEY_A:
            return shifted ? 'A' : 'a';
        case KEY_B:
            return shifted ? 'B' : 'b';
        case KEY_C:
            return shifted ? 'C' : 'c';
        case KEY_D:
            return shifted ? 'D' : 'd';
        case KEY_E:
            return shifted ? 'E' : 'e';
        case KEY_F:
            return shifted ? 'F' : 'f';
        case KEY_G:
            return shifted ? 'G' : 'g';
        case KEY_H:
            return shifted ? 'H' : 'h';
        case KEY_I:
            return shifted ? 'I' : 'i';
        case KEY_J:
            return shifted ? 'J' : 'j';
        case KEY_K:
            return shifted ? 'K' : 'k';
        case KEY_L:
            return shifted ? 'L' : 'l';
        case KEY_M:
            return shifted ? 'M' : 'm';
        case KEY_N:
            return shifted ? 'N' : 'n';
        case KEY_O:
            return shifted ? 'O' : 'o';
        case KEY_P:
            return shifted ? 'P' : 'p';
        case KEY_Q:
            return shifted ? 'Q' : 'q';
        case KEY_R:
            return shifted ? 'R' : 'r';
        case KEY_S:
            return shifted ? 'S' : 's';
        case KEY_T:
            return shifted ? 'T' : 't';
        case KEY_U:
            return shifted ? 'U' : 'u';
        case KEY_V:
            return shifted ? 'V' : 'v';
        case KEY_W:
            return shifted ? 'W' : 'w';
        case KEY_X:
            return shifted ? 'X' : 'x';
        case KEY_Y:
            return shifted ? 'Y' : 'y';
        case KEY_Z:
            return shifted ? 'Z' : 'z';
        case KEY_SPACE:
            return ' ';
        case KEY_TAB:
            return LV_KEY_NEXT;
        case KEY_0:
        case KEY_KP0:
            return shifted ? ')' : '0';
        case KEY_1:
        case KEY_KP1:
            return shifted ? '!' : '1';
        case KEY_2:
        case KEY_KP2:
            return shifted ? '@' : '2';
        case KEY_3:
        case KEY_KP3:
            return shifted ? '#' : '3';
        case KEY_4:
        case KEY_KP4:
            return shifted ? '$' : '4';
        case KEY_5:
        case KEY_KP5:
            return shifted ? '%' : '5';
        case KEY_6:
        case KEY_KP6:
            return shifted ? '^' : '6';
        case KEY_7:
        case KEY_KP7:
            return shifted ? '&' : '7';
        case KEY_8:
        case KEY_KP8:
            return shifted ? '*' : '8';
        case KEY_9:
        case KEY_KP9:
            return shifted ? '(' : '9';
        case KEY_KPDOT:
            return '.';
        case KEY_KPCOMMA:
        case KEY_KPJPCOMMA:
            return ',';
        case KEY_KPMINUS:
            return '-';
        case KEY_KPPLUS:
            return '+';
        case KEY_KPASTERISK:
            return '*';
        case KEY_KPSLASH:
            return '/';
        case KEY_KPEQUAL:
            return '=';
        case KEY_KPLEFTPAREN:
            return '(';
        case KEY_KPRIGHTPAREN:
            return ')';
        case KEY_MINUS:
            return shifted ? '_' : '-';
        case KEY_EQUAL:
            return shifted ? '+' : '=';
        case KEY_LEFTBRACE:
            return shifted ? '{' : '[';
        case KEY_RIGHTBRACE:
            return shifted ? '}' : ']';
        case KEY_BACKSLASH:
            return shifted ? '|' : '\\';
        case KEY_SEMICOLON:
            return shifted ? ':' : ';';
        case KEY_APOSTROPHE:
            return shifted ? '"' : '\'';
        case KEY_GRAVE:
            return shifted ? '~' : '`';
        case KEY_COMMA:
            return shifted ? '<' : ',';
        case KEY_DOT:
            return shifted ? '>' : '.';
        case KEY_SLASH:
            return shifted ? '?' : '/';
        case KEY_102ND:
            return shifted ? '>' : '<';
        case KEY_YEN:
            return shifted ? '|' : '\\';
        case KEY_RO:
            return shifted ? '_' : '\\';
        default:
            return 0;
    }
#else
    (void)code;
    return 0;
#endif
}

bool NfcKeypad::shiftPressed() const
{
    return _left_shift_pressed || _right_shift_pressed;
}

const char* NfcKeypad::keyUtf8(uint32_t key) const
{
    static char text[2] = {0, 0};
    if (key >= 0x20 && key < 0x7f) {
        text[0] = static_cast<char>(key);
        text[1] = '\0';
        return text;
    }
    return "";
}

}  // namespace cap_nfc

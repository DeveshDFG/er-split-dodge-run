#pragma once

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

namespace er_dash_key_parse {

inline constexpr const char* kSupportedKeysList =
    "LeftShift, RightShift, Shift, LeftCtrl, RightCtrl, Ctrl, LeftAlt, RightAlt, "
    "Alt, Space, F, A-Z, 0-9";

enum class poll_kind {
    single,
    shift_any,
    ctrl_any,
    alt_any,
};

struct parsed_dash_key {
    bool ok{ false };
    std::string normalized_name;
    int primary_vk{ -1 };
    poll_kind poll{ poll_kind::single };
};

inline std::string to_lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

inline std::string trim_ascii(std::string value) {
    while (!value.empty()
        && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'
            || value.front() == '\n')) {
        value.erase(value.begin());
    }
    while (!value.empty()
        && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'
            || value.back() == '\n')) {
        value.pop_back();
    }
    return value;
}

inline parsed_dash_key parse_dash_key(std::string name) {
    parsed_dash_key result{};
    name = trim_ascii(std::move(name));
    if (name.empty()) {
        return result;
    }

    const std::string key = to_lower_ascii(name);
    result.normalized_name = name;

    auto single = [&](int vk, std::string display) -> parsed_dash_key {
        parsed_dash_key out{};
        out.ok = true;
        out.normalized_name = std::move(display);
        out.primary_vk = vk;
        out.poll = poll_kind::single;
        return out;
    };

    if (key == "leftshift") {
        return single(VK_LSHIFT, "LeftShift");
    }
    if (key == "rightshift") {
        return single(VK_RSHIFT, "RightShift");
    }
    if (key == "shift") {
        parsed_dash_key out{};
        out.ok = true;
        out.normalized_name = "Shift";
        out.primary_vk = VK_SHIFT;
        out.poll = poll_kind::shift_any;
        return out;
    }
    if (key == "leftctrl" || key == "lctrl") {
        return single(VK_LCONTROL, "LeftCtrl");
    }
    if (key == "rightctrl" || key == "rctrl") {
        return single(VK_RCONTROL, "RightCtrl");
    }
    if (key == "ctrl" || key == "control") {
        parsed_dash_key out{};
        out.ok = true;
        out.normalized_name = "Ctrl";
        out.primary_vk = VK_CONTROL;
        out.poll = poll_kind::ctrl_any;
        return out;
    }
    if (key == "leftalt" || key == "lalt") {
        return single(VK_LMENU, "LeftAlt");
    }
    if (key == "rightalt" || key == "ralt") {
        return single(VK_RMENU, "RightAlt");
    }
    if (key == "alt" || key == "menu") {
        parsed_dash_key out{};
        out.ok = true;
        out.normalized_name = "Alt";
        out.primary_vk = VK_MENU;
        out.poll = poll_kind::alt_any;
        return out;
    }
    if (key == "space") {
        return single(VK_SPACE, "Space");
    }
    if (key == "f") {
        return single(0x46, "F");
    }

    if (name.size() == 1) {
        const char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        if (ch >= 'A' && ch <= 'Z') {
            return single(ch, std::string(1, ch));
        }
        if (ch >= '0' && ch <= '9') {
            return single(ch, std::string(1, ch));
        }
    }

    return result;
}

inline bool is_key_down(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

inline bool query_held(const parsed_dash_key& config) {
    if (!config.ok || config.primary_vk < 0) {
        return false;
    }
    switch (config.poll) {
    case poll_kind::single:
        return is_key_down(config.primary_vk);
    case poll_kind::shift_any:
        return is_key_down(VK_SHIFT) || is_key_down(VK_LSHIFT) || is_key_down(VK_RSHIFT);
    case poll_kind::ctrl_any:
        return is_key_down(VK_CONTROL) || is_key_down(VK_LCONTROL)
            || is_key_down(VK_RCONTROL);
    case poll_kind::alt_any:
        return is_key_down(VK_MENU) || is_key_down(VK_LMENU) || is_key_down(VK_RMENU);
    }
    return false;
}

} // namespace er_dash_key_parse

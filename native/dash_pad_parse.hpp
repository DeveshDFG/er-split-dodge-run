#pragma once

#include "dash_key_parse.hpp"
#include "dash_xinput.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace dash_pad_parse {

inline constexpr const char* kSupportedButtonsList =
    "A/Cross, B/Circle, X/Square, Y/Triangle, LB/L1, RB/R1, LT/L2, RT/R2, "
    "LS/L3/LeftStick, RS/R3/RightStick, DPadUp/Up, DPadDown/Down, DPadLeft/Left, "
    "DPadRight/Right, Start/Options, Back/Share/Select";

enum class button_kind {
    none,
    digital,
    left_trigger,
    right_trigger,
};

enum class gamepad_select {
    any = -1,
    user0 = 0,
    user1 = 1,
    user2 = 2,
    user3 = 3,
};

struct parsed_dash_button {
    bool configured{ false };
    bool ok{ false };
    bool is_left_stick_click{ false };
    std::string normalized_name;
    std::string xinput_resolve_name;
    button_kind kind{ button_kind::none };
    WORD button_mask{ 0 };
};

struct held_snapshot {
    bool held{ false };
    bool from_keyboard{ false };
    bool from_xinput{ false };
    int pad_index{ -1 };
    std::string keyboard_label;
    std::string button_label;
};

struct button_dash_result {
    bool dash_held{ false };
    bool neutral_held{ false };
    int pad_index{ -1 };
    int stick_magnitude{ 0 };
};

inline std::string to_lower_ascii(std::string value) {
    return er_dash_key_parse::to_lower_ascii(std::move(value));
}

inline std::string trim_ascii(std::string value) {
    return er_dash_key_parse::trim_ascii(std::move(value));
}

inline parsed_dash_button make_digital(std::string display, std::string resolve, WORD mask) {
    parsed_dash_button out{};
    out.configured = true;
    out.ok = true;
    out.normalized_name = std::move(display);
    out.xinput_resolve_name = std::move(resolve);
    out.kind = button_kind::digital;
    out.button_mask = mask;
    out.is_left_stick_click = (mask == XINPUT_GAMEPAD_LEFT_THUMB);
    return out;
}

inline bool is_left_stick_click_button(const parsed_dash_button& button) {
    return button.ok && button.is_left_stick_click;
}

inline parsed_dash_button make_trigger(std::string display, std::string resolve,
    button_kind trigger_kind) {
    parsed_dash_button out{};
    out.configured = true;
    out.ok = true;
    out.normalized_name = std::move(display);
    out.xinput_resolve_name = std::move(resolve);
    out.kind = trigger_kind;
    return out;
}

inline parsed_dash_button parse_dash_button(std::string name) {
    parsed_dash_button result{};
    name = trim_ascii(std::move(name));
    if (name.empty()) {
        return result;
    }

    result.configured = true;
    const std::string key = to_lower_ascii(name);

    if (key == "a" || key == "cross") {
        return make_digital("A", "A", XINPUT_GAMEPAD_A);
    }
    if (key == "b" || key == "circle") {
        return make_digital("B", "B", XINPUT_GAMEPAD_B);
    }
    if (key == "x" || key == "square") {
        return make_digital("X", "X", XINPUT_GAMEPAD_X);
    }
    if (key == "y" || key == "triangle") {
        return make_digital("Y", "Y", XINPUT_GAMEPAD_Y);
    }
    if (key == "lb" || key == "l1") {
        return make_digital("L1", "LEFT_SHOULDER", XINPUT_GAMEPAD_LEFT_SHOULDER);
    }
    if (key == "rb" || key == "r1") {
        return make_digital("R1", "RIGHT_SHOULDER", XINPUT_GAMEPAD_RIGHT_SHOULDER);
    }
    if (key == "lt" || key == "l2") {
        return make_trigger("L2", "LEFT_TRIGGER", button_kind::left_trigger);
    }
    if (key == "rt" || key == "r2") {
        return make_trigger("R2", "RIGHT_TRIGGER", button_kind::right_trigger);
    }
    if (key == "ls" || key == "l3" || key == "leftstick" || key == "leftstickclick") {
        return make_digital("L3", "LEFT_THUMB", XINPUT_GAMEPAD_LEFT_THUMB);
    }
    if (key == "rs" || key == "r3" || key == "rightstick" || key == "rightstickclick") {
        return make_digital("R3", "RIGHT_THUMB", XINPUT_GAMEPAD_RIGHT_THUMB);
    }
    if (key == "dpadup" || key == "up") {
        return make_digital("DPadUp", "DPAD_UP", XINPUT_GAMEPAD_DPAD_UP);
    }
    if (key == "dpaddown" || key == "down") {
        return make_digital("DPadDown", "DPAD_DOWN", XINPUT_GAMEPAD_DPAD_DOWN);
    }
    if (key == "dpadleft" || key == "left") {
        return make_digital("DPadLeft", "DPAD_LEFT", XINPUT_GAMEPAD_DPAD_LEFT);
    }
    if (key == "dpadright" || key == "right") {
        return make_digital("DPadRight", "DPAD_RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT);
    }
    if (key == "start" || key == "options") {
        return make_digital("Start", "START", XINPUT_GAMEPAD_START);
    }
    if (key == "back" || key == "share" || key == "select") {
        return make_digital("Back", "BACK", XINPUT_GAMEPAD_BACK);
    }

    result.normalized_name = name;
    return result;
}

inline std::optional<gamepad_select> parse_gamepad_index(std::string value) {
    value = trim_ascii(std::move(value));
    if (value.empty()) {
        return gamepad_select::any;
    }
    const std::string key = to_lower_ascii(value);
    if (key == "any") {
        return gamepad_select::any;
    }
    if (key.size() == 1 && key[0] >= '0' && key[0] <= '3') {
        return static_cast<gamepad_select>(key[0] - '0');
    }
    return std::nullopt;
}

inline bool is_pad_button_held(const XINPUT_GAMEPAD& pad, const parsed_dash_button& button,
    int trigger_threshold) {
    if (!button.ok) {
        return false;
    }
    switch (button.kind) {
    case button_kind::digital:
        return (pad.wButtons & button.button_mask) == button.button_mask;
    case button_kind::left_trigger:
        return static_cast<int>(pad.bLeftTrigger) >= trigger_threshold;
    case button_kind::right_trigger:
        return static_cast<int>(pad.bRightTrigger) >= trigger_threshold;
    default:
        return false;
    }
}

inline bool query_xinput_held(const parsed_dash_button& button, gamepad_select pad_select,
    int trigger_threshold, int& held_pad_index_out) {
    held_pad_index_out = -1;
    if (!button.ok || !dash_xinput::available()) {
        return false;
    }

    const auto try_user = [&](DWORD user_index) -> bool {
        XINPUT_STATE state{};
        if (!dash_xinput::read_pad_state(user_index, state)) {
            return false;
        }
        if (!is_pad_button_held(state.Gamepad, button, trigger_threshold)) {
            return false;
        }
        held_pad_index_out = static_cast<int>(user_index);
        return true;
    };

    if (pad_select != gamepad_select::any) {
        return try_user(static_cast<DWORD>(static_cast<int>(pad_select)));
    }

    for (DWORD user = 0; user < XUSER_MAX_COUNT; ++user) {
        if (try_user(user)) {
            return true;
        }
    }
    return false;
}

inline bool query_keyboard_held(const er_dash_key_parse::parsed_dash_key& key) {
    return key.ok && er_dash_key_parse::query_held(key);
}

inline int left_stick_magnitude_squared(const XINPUT_GAMEPAD& pad) {
    const int x = static_cast<int>(pad.sThumbLX);
    const int y = static_cast<int>(pad.sThumbLY);
    return x * x + y * y;
}

inline int left_stick_magnitude(const XINPUT_GAMEPAD& pad) {
    const int mag_sq = left_stick_magnitude_squared(pad);
    return static_cast<int>(std::sqrt(static_cast<double>(mag_sq)));
}

inline button_dash_result query_button_dash(const parsed_dash_button& button,
    gamepad_select pad_select, int trigger_threshold, int left_stick_deadzone) {
    button_dash_result result{};
    if (!button.ok || !dash_xinput::available()) {
        return result;
    }

    const int deadzone_sq = left_stick_deadzone * left_stick_deadzone;

    const auto try_user = [&](DWORD user_index) -> bool {
        XINPUT_STATE state{};
        if (!dash_xinput::read_pad_state(user_index, state)) {
            return false;
        }
        if (!is_pad_button_held(state.Gamepad, button, trigger_threshold)) {
            return false;
        }

        const XINPUT_GAMEPAD& pad = state.Gamepad;
        const int mag_sq = left_stick_magnitude_squared(pad);
        result.pad_index = static_cast<int>(user_index);
        result.stick_magnitude = left_stick_magnitude(pad);

        if (mag_sq > deadzone_sq) {
            result.dash_held = true;
        } else {
            result.neutral_held = true;
        }
        return true;
    };

    if (pad_select != gamepad_select::any) {
        try_user(static_cast<DWORD>(static_cast<int>(pad_select)));
        return result;
    }

    for (DWORD user = 0; user < XUSER_MAX_COUNT; ++user) {
        if (try_user(user)) {
            return result;
        }
    }
    return result;
}

inline bool query_button_held(const parsed_dash_button& button, gamepad_select pad_select,
    int trigger_threshold, int left_stick_deadzone, int& held_pad_index_out) {
    const button_dash_result result =
        query_button_dash(button, pad_select, trigger_threshold, left_stick_deadzone);
    held_pad_index_out = result.pad_index;
    return result.dash_held;
}

inline held_snapshot query_dash_held(const er_dash_key_parse::parsed_dash_key& key,
    const parsed_dash_button& button, gamepad_select pad_select, int trigger_threshold,
    int left_stick_deadzone) {
    held_snapshot snap{};

    if (query_keyboard_held(key)) {
        snap.held = true;
        snap.from_keyboard = true;
        snap.keyboard_label = key.normalized_name;
    }

    const button_dash_result button_result =
        query_button_dash(button, pad_select, trigger_threshold, left_stick_deadzone);
    if (button_result.dash_held) {
        snap.held = true;
        snap.from_xinput = true;
        snap.pad_index = button_result.pad_index;
        snap.button_label = button.normalized_name;
    }

    return snap;
}

} // namespace dash_pad_parse

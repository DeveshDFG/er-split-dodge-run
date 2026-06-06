#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "dash_key_parse.hpp"
#include "dash_pad_parse.hpp"

#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace bridge_config {

inline constexpr const char* kIniNamePreferred = "separate_roll_and_sprint.ini";
inline constexpr const char* kIniNameLegacy = "ERKeyAssignDashInputBridge.ini";
inline constexpr const char* kBindDirEnv = "ER_SPLIT_BIND_DIR";
inline constexpr const char* kMe3ProfileEnv = "ER_SPLIT_ME3_PROFILE";
inline constexpr const char* kDefaultMe3Profile = "bindtest";

struct ini_config {
    std::string dash_key;
    bool has_dash_key{ false };
    int dash_vk{ -1 };
    bool has_dash_vk{ false };
    std::string dash_button;
    bool has_dash_button{ false };
    std::string gamepad_index_raw;
    bool has_gamepad_index{ false };
    int dash_trigger_threshold{ 80 };
    bool has_dash_trigger_threshold{ false };
    int left_stick_dash_deadzone{ 12000 };
    bool has_left_stick_dash_deadzone{ false };
    std::string move_forward{ "W" };
    bool has_move_forward{ false };
    std::string move_back{ "S" };
    bool has_move_back{ false };
    std::string move_left{ "A" };
    bool has_move_left{ false };
    std::string move_right{ "D" };
    bool has_move_right{ false };
    int movement_stick_deadzone{ 12000 };
    bool has_movement_stick_deadzone{ false };
    bool enable_menu_patch{ false };
};

inline std::string join_path(std::string dir, const char* filename) {
    if (!dir.empty() && dir.back() != '\\' && dir.back() != '/') {
        dir.push_back('\\');
    }
    dir += filename;
    return dir;
}

inline std::string directory_from_module_path(const char* module_path) {
    std::string path(module_path);
    const auto slash = path.find_last_of("\\/");
    if (slash != std::string::npos) {
        path.resize(slash + 1);
    }
    return path;
}

inline std::string module_dir(HMODULE module) {
    char module_path[MAX_PATH]{};
    if (GetModuleFileNameA(module, module_path, MAX_PATH) == 0) {
        return {};
    }
    return directory_from_module_path(module_path);
}

inline bool file_exists(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    const DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

inline std::string me3_bind_dir() {
    char local_app_data[MAX_PATH]{};
    if (GetEnvironmentVariableA("LOCALAPPDATA", local_app_data, MAX_PATH) == 0) {
        return {};
    }

    char profile[MAX_PATH]{};
    const DWORD profile_len =
        GetEnvironmentVariableA(kMe3ProfileEnv, profile, MAX_PATH);
    const char* profile_name =
        (profile_len > 0 && profile_len < MAX_PATH) ? profile : kDefaultMe3Profile;

    std::string dir = local_app_data;
    dir += "\\garyttierney\\me3\\config\\profiles\\";
    dir += profile_name;
    dir += "\\bind\\";
    return dir;
}

inline bool try_resolve_ini_in_base(const std::string& base, std::string& config_dir_out,
    std::string& ini_path_out) {
    if (base.empty()) {
        return false;
    }
    for (const char* name : { kIniNamePreferred, kIniNameLegacy }) {
        const std::string candidate = join_path(base, name);
        if (file_exists(candidate)) {
            config_dir_out = base;
            ini_path_out = candidate;
            return true;
        }
    }
    return false;
}

inline std::string resolve_ini_path(HMODULE module, std::string& config_dir_out) {
    std::vector<std::string> bases;

    char bind_dir_env[MAX_PATH]{};
    if (GetEnvironmentVariableA(kBindDirEnv, bind_dir_env, MAX_PATH) > 0) {
        bases.push_back(bind_dir_env);
    }

    const std::string me3_bind = me3_bind_dir();
    if (!me3_bind.empty()) {
        bases.push_back(me3_bind);
    }

    const std::string dll_dir = module_dir(module);
    if (!dll_dir.empty()) {
        bases.push_back(dll_dir);
    }

    char exe_path[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) != 0) {
        bases.push_back(directory_from_module_path(exe_path));
    }

    for (const auto& base : bases) {
        std::string candidate_path;
        if (try_resolve_ini_in_base(base, config_dir_out, candidate_path)) {
            return candidate_path;
        }
    }

    if (!bases.empty() && !bases.front().empty()) {
        config_dir_out = bases.front();
        return join_path(bases.front(), kIniNamePreferred);
    }

    config_dir_out.clear();
    return kIniNamePreferred;
}

inline bool parse_bool(const std::string& value, bool default_value) {
    const std::string lowered = er_dash_key_parse::to_lower_ascii(value);
    if (lowered == "1" || lowered == "true" || lowered == "yes") {
        return true;
    }
    if (lowered == "0" || lowered == "false" || lowered == "no") {
        return false;
    }
    return default_value;
}

inline ini_config read_ini(const std::string& ini_path) {
    ini_config cfg{};
    std::ifstream ini(ini_path);
    if (!ini) {
        return cfg;
    }

    std::string line;
    while (std::getline(ini, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) {
            line.resize(hash);
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        std::string key = er_dash_key_parse::trim_ascii(line.substr(0, eq));
        std::string value = er_dash_key_parse::trim_ascii(line.substr(eq + 1));
        const std::string key_lower = er_dash_key_parse::to_lower_ascii(key);

        if (key_lower == "dash_key") {
            cfg.dash_key = value;
            cfg.has_dash_key = !value.empty();
        } else if (key_lower == "dash_vk") {
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(value.c_str(), &end, 0);
            if (end != value.c_str()) {
                cfg.dash_vk = static_cast<int>(parsed);
                cfg.has_dash_vk = true;
            }
        } else if (key_lower == "dash_button") {
            cfg.dash_button = value;
            cfg.has_dash_button = !value.empty();
        } else if (key_lower == "gamepad_index") {
            cfg.gamepad_index_raw = value;
            cfg.has_gamepad_index = !value.empty();
        } else if (key_lower == "dash_trigger_threshold") {
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
            if (end != value.c_str()) {
                int threshold = static_cast<int>(parsed);
                if (threshold < 0) {
                    threshold = 0;
                }
                if (threshold > 255) {
                    threshold = 255;
                }
                cfg.dash_trigger_threshold = threshold;
                cfg.has_dash_trigger_threshold = true;
            }
        } else if (key_lower == "left_stick_dash_deadzone") {
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
            if (end != value.c_str()) {
                int deadzone = static_cast<int>(parsed);
                if (deadzone < 0) {
                    deadzone = 0;
                }
                if (deadzone > 32767) {
                    deadzone = 32767;
                }
                cfg.left_stick_dash_deadzone = deadzone;
                cfg.has_left_stick_dash_deadzone = true;
            }
        } else if (key_lower == "move_forward") {
            cfg.move_forward = value;
            cfg.has_move_forward = !value.empty();
        } else if (key_lower == "move_back") {
            cfg.move_back = value;
            cfg.has_move_back = !value.empty();
        } else if (key_lower == "move_left") {
            cfg.move_left = value;
            cfg.has_move_left = !value.empty();
        } else if (key_lower == "move_right") {
            cfg.move_right = value;
            cfg.has_move_right = !value.empty();
        } else if (key_lower == "movement_stick_deadzone") {
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
            if (end != value.c_str()) {
                int deadzone = static_cast<int>(parsed);
                if (deadzone < 0) {
                    deadzone = 0;
                }
                if (deadzone > 32767) {
                    deadzone = 32767;
                }
                cfg.movement_stick_deadzone = deadzone;
                cfg.has_movement_stick_deadzone = true;
            }
        } else if (key_lower == "enable_menu_patch") {
            cfg.enable_menu_patch = parse_bool(value, false);
        }
    }
    return cfg;
}

inline dash_pad_parse::parsed_dash_button
parse_active_dash_button(const ini_config& cfg) {
    if (!cfg.has_dash_button) {
        return {};
    }
    dash_pad_parse::parsed_dash_button parsed =
        dash_pad_parse::parse_dash_button(cfg.dash_button);
    if (!parsed.ok) {
        parsed.configured = true;
        parsed.normalized_name = cfg.dash_button;
    }
    return parsed;
}

inline dash_pad_parse::gamepad_select
parse_active_gamepad_index(const ini_config& cfg) {
    if (!cfg.has_gamepad_index) {
        return dash_pad_parse::gamepad_select::any;
    }
    const auto parsed = dash_pad_parse::parse_gamepad_index(cfg.gamepad_index_raw);
    if (!parsed) {
        return dash_pad_parse::gamepad_select::any;
    }
    return *parsed;
}

inline er_dash_key_parse::parsed_dash_key
parse_movement_key(const std::string& key_name, const char* default_name) {
    const std::string configured = key_name.empty() ? std::string(default_name) : key_name;
    er_dash_key_parse::parsed_dash_key parsed =
        er_dash_key_parse::parse_dash_key(configured);
    if (!parsed.ok) {
        parsed = er_dash_key_parse::parse_dash_key(default_name);
    }
    return parsed;
}

inline std::optional<er_dash_key_parse::parsed_dash_key>
parse_active_dash_key(const ini_config& cfg) {
    if (cfg.has_dash_key) {
        return er_dash_key_parse::parse_dash_key(cfg.dash_key);
    }
    if (cfg.has_dash_vk) {
        er_dash_key_parse::parsed_dash_key parsed{};
        parsed.ok = true;
        parsed.normalized_name = "dash_vk";
        parsed.primary_vk = cfg.dash_vk;
        parsed.poll = er_dash_key_parse::poll_kind::single;
        return parsed;
    }
    return std::nullopt;
}

} // namespace bridge_config

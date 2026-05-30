// Sprint-only dash bridge: INI dash_key + dash_button + HKS ERSplit_IsDashHeldNative.
// No runtime dependency on libER.dll (links MinHook only).

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <MinHook.h>

#include "bridge_config.hpp"
#include "dash_key_parse.hpp"
#include "dash_pad_parse.hpp"
#include "dash_xinput.hpp"
#include "hks_pattern_scan.hpp"

#include <atomic>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace {

using HksState = void;

using hks_lua_pushnumber_fn = void (*)(HksState* state, float number);
using hks_addnamedcclosure_fn = void (*)(HksState* state, const char* name,
    void* func);
using hks_set_cglobals_fn = void (*)(HksState* state);

constexpr const char* kNativeDashHeld = "ERSplit_IsDashHeldNative";
constexpr const char* kNativeDashKeyHeld = "ERSplit_IsDashKeyHeldNative";
constexpr const char* kNativeDashButtonHeld = "ERSplit_IsDashButtonHeldNative";
constexpr const char* kNativeDashLeftStickClick = "ERSplit_IsDashLeftStickClickNative";
constexpr const char* kNativeDashButtonNeutralHeld = "ERSplit_IsDashButtonNeutralHeldNative";
constexpr const char* kNativeDashButtonConfigured = "ERSplit_IsDashButtonConfiguredNative";
constexpr const char* kNativeDashButtonPressed = "ERSplit_IsDashButtonPressedNative";
constexpr int kHotkeyReloadIni = VK_F10;

std::mutex g_log_mutex;
std::string g_log_path;
std::string g_ini_path;

std::atomic<int> g_last_logged_held{ -1 };
std::atomic<bool> g_hks_registration_logged{ false };
std::atomic<bool> g_hotkey_f10_down{ false };

er_dash_key_parse::parsed_dash_key g_active_dash_key{};
dash_pad_parse::parsed_dash_button g_active_dash_button{};
dash_pad_parse::gamepad_select g_gamepad_select{ dash_pad_parse::gamepad_select::any };
int g_trigger_threshold{ 80 };
int g_left_stick_dash_deadzone{ 12000 };
bool g_dash_button_is_left_stick_click{ false };
bool g_dash_button_configured{ false };

std::atomic<int> g_last_logged_keyboard_held{ -1 };
std::atomic<int> g_last_logged_button_held{ -1 };
std::atomic<int> g_last_logged_button_neutral_held{ -1 };

hks_lua_pushnumber_fn g_hks_lua_pushnumber = nullptr;
hks_addnamedcclosure_fn g_hks_addnamedcclosure = nullptr;
hks_set_cglobals_fn g_hks_set_cglobals = nullptr;
hks_set_cglobals_fn g_hks_set_cglobals_original = nullptr;

void append_log(const std::string& line) {
    std::lock_guard lock(g_log_mutex);
    std::ofstream file(g_log_path, std::ios::app);
    if (file) {
        file << line;
        if (line.empty() || line.back() != '\n') {
            file << '\n';
        }
    }
}

void write_log_header(const std::string& body) {
    std::lock_guard lock(g_log_mutex);
    std::ofstream file(g_log_path, std::ios::trunc);
    if (file) {
        file << body;
        if (!body.empty() && body.back() != '\n') {
            file << '\n';
        }
    }
}

std::string format_vk_hex(int vk) {
    if (vk < 0) {
        return "none";
    }
    std::ostringstream out;
    out << "0x" << std::hex << vk << std::dec;
    return out.str();
}

std::string dash_key_config_label() {
    if (g_active_dash_key.ok) {
        return g_active_dash_key.normalized_name;
    }
    return "none";
}

std::string dash_button_config_label() {
    if (g_active_dash_button.ok) {
        return g_active_dash_button.normalized_name;
    }
    if (g_active_dash_button.configured) {
        return "invalid";
    }
    return "none";
}

std::string xinput_status_label() {
    if (!dash_xinput::available()) {
        return "unavailable";
    }
    return "loaded dll=" + dash_xinput::g_dll_name;
}

void log_xinput_boot_line(std::ostringstream& boot) {
    std::string dll_name;
    if (dash_xinput::init(&dll_name)) {
        boot << "xinput=loaded dll=" << dll_name << '\n';
    } else {
        boot << "xinput=unavailable\n";
    }
}

bool load_dash_config_from_ini(std::ostringstream& boot, const bridge_config::ini_config& cfg) {
    if (cfg.enable_menu_patch) {
        boot << "enable_menu_patch=1 ignored by this DLL\n"
             << "menu_patch_note=optional libER menu diagnostic is not part of this "
                "shipped bridge\n";
    }

    g_active_dash_key = {};
    g_active_dash_button = {};
    g_gamepad_select = bridge_config::parse_active_gamepad_index(cfg);
    g_trigger_threshold =
        cfg.has_dash_trigger_threshold ? cfg.dash_trigger_threshold : 80;
    g_left_stick_dash_deadzone = cfg.has_left_stick_dash_deadzone
        ? cfg.left_stick_dash_deadzone
        : 12000;

    bool key_ok = false;
    if (cfg.has_dash_key || cfg.has_dash_vk) {
        const auto parsed_key = bridge_config::parse_active_dash_key(cfg);
        if (parsed_key && parsed_key->ok) {
            g_active_dash_key = *parsed_key;
            key_ok = true;
        } else if (cfg.has_dash_key) {
            boot << "config_error unknown_dash_key=\"" << cfg.dash_key << "\"\n"
                 << "supported_keys=" << er_dash_key_parse::kSupportedKeysList << '\n';
        }
    }

    bool button_ok = false;
    if (cfg.has_dash_button) {
        g_active_dash_button = bridge_config::parse_active_dash_button(cfg);
        if (g_active_dash_button.ok) {
            button_ok = true;
        } else {
            boot << "config_error unknown_dash_button=\"" << cfg.dash_button << "\"\n"
                 << "supported_dash_buttons=" << dash_pad_parse::kSupportedButtonsList
                 << '\n';
        }
    }

    boot << "config_loaded dash_key=" << dash_key_config_label()
         << " dash_button=" << dash_button_config_label() << ' '
         << xinput_status_label() << '\n';

    g_dash_button_configured = g_active_dash_button.ok;
    g_dash_button_is_left_stick_click =
        dash_pad_parse::is_left_stick_click_button(g_active_dash_button);

    if (g_active_dash_button.ok) {
        boot << "dash_button_resolved name=" << g_active_dash_button.normalized_name
             << " xinput=" << g_active_dash_button.xinput_resolve_name
             << " special=stick_gated_neutral_action\n";
        boot << "left_stick_dash_deadzone=" << g_left_stick_dash_deadzone << '\n';
    }

    if (cfg.has_gamepad_index) {
        boot << "gamepad_index=" << cfg.gamepad_index_raw << '\n';
    } else {
        boot << "gamepad_index=any\n";
    }
    boot << "dash_trigger_threshold=" << g_trigger_threshold << '\n';

    if (key_ok || button_ok) {
        boot << "sprint_source=ini dash_key and/or dash_button\n";
        return true;
    }

    if (!cfg.has_dash_key && !cfg.has_dash_button && !cfg.has_dash_vk) {
        boot << "config_note=no dash_key or dash_button configured\n";
    }
    return false;
}

bool query_keyboard_held() {
    return dash_pad_parse::query_keyboard_held(g_active_dash_key);
}

dash_pad_parse::button_dash_result query_button_state() {
    return dash_pad_parse::query_button_dash(g_active_dash_button, g_gamepad_select,
        g_trigger_threshold, g_left_stick_dash_deadzone);
}

bool query_button_held(int& pad_index) {
    const dash_pad_parse::button_dash_result result = query_button_state();
    pad_index = result.pad_index;
    return result.dash_held;
}

bool query_button_neutral_held(int& pad_index) {
    const dash_pad_parse::button_dash_result result = query_button_state();
    pad_index = result.pad_index;
    return result.neutral_held;
}

bool query_button_pressed(int& pad_index) {
    const dash_pad_parse::button_dash_result result = query_button_state();
    pad_index = result.pad_index;
    return result.dash_held || result.neutral_held;
}

void log_keyboard_transition(bool held) {
    const int held_i = held ? 1 : 0;
    const int prev = g_last_logged_keyboard_held.exchange(held_i);
    if (prev == held_i) {
        return;
    }
    std::ostringstream line;
    line << "native_dash_held transition=" << (held ? "down" : "up")
         << " source=keyboard key=" << g_active_dash_key.normalized_name;
    append_log(line.str());
}

void log_button_transition(bool held, const dash_pad_parse::button_dash_result& result) {
    const int held_i = held ? 1 : 0;
    const int prev = g_last_logged_button_held.exchange(held_i);
    if (prev == held_i) {
        return;
    }
    std::ostringstream line;
    line << "native_dash_held transition=" << (held ? "down" : "up")
         << " source=xinput index=" << result.pad_index
         << " button=" << g_active_dash_button.normalized_name
         << " stick_mag=" << result.stick_magnitude;
    append_log(line.str());
}

void log_button_neutral_transition(bool held, const dash_pad_parse::button_dash_result& result) {
    const int held_i = held ? 1 : 0;
    const int prev = g_last_logged_button_neutral_held.exchange(held_i);
    if (prev == held_i) {
        return;
    }
    std::ostringstream line;
    line << "native_dash_button_neutral transition=" << (held ? "down" : "up")
         << " button=" << g_active_dash_button.normalized_name
         << " stick_mag=" << result.stick_magnitude;
    append_log(line.str());
}

void log_input_transitions() {
    if (g_active_dash_key.ok) {
        log_keyboard_transition(query_keyboard_held());
    }

    if (g_active_dash_button.ok) {
        const dash_pad_parse::button_dash_result result = query_button_state();
        log_button_transition(result.dash_held, result);
        log_button_neutral_transition(result.neutral_held, result);
    }

    int pad_index = -1;
    const bool held = query_keyboard_held() || query_button_held(pad_index);
    g_last_logged_held.exchange(held ? 1 : 0);
}

void push_number(HksState* state, bool value) {
    g_hks_lua_pushnumber(state, value ? 1.f : 0.f);
}

bool poll_hotkey_edge(int vk, std::atomic<bool>& was_down) {
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool prev = was_down.exchange(down);
    return down && !prev;
}

void reload_ini_hotkey() {
    std::ostringstream out;
    out << "\n========== F10 INI reload ==========\n";
    log_xinput_boot_line(out);
    const bridge_config::ini_config cfg = bridge_config::read_ini(g_ini_path);
    load_dash_config_from_ini(out, cfg);
    out << "libER_runtime_dependency=none\n";
    append_log(out.str());
}

int lua_ERSplit_IsDashKeyHeldNative(HksState* state) {
    if (!g_hks_lua_pushnumber) {
        return 0;
    }
    push_number(state, query_keyboard_held());
    return 1;
}

int lua_ERSplit_IsDashButtonHeldNative(HksState* state) {
    if (!g_hks_lua_pushnumber) {
        return 0;
    }
    int pad_index = -1;
    push_number(state, query_button_held(pad_index));
    return 1;
}

int lua_ERSplit_IsDashButtonNeutralHeldNative(HksState* state) {
    if (!g_hks_lua_pushnumber) {
        return 0;
    }
    int pad_index = -1;
    push_number(state, query_button_neutral_held(pad_index));
    return 1;
}

int lua_ERSplit_IsDashButtonPressedNative(HksState* state) {
    if (!g_hks_lua_pushnumber) {
        return 0;
    }
    int pad_index = -1;
    push_number(state, query_button_pressed(pad_index));
    return 1;
}

int lua_ERSplit_IsDashButtonConfiguredNative(HksState* state) {
    if (!g_hks_lua_pushnumber) {
        return 0;
    }
    push_number(state, g_dash_button_configured);
    return 1;
}

int lua_ERSplit_IsDashLeftStickClickNative(HksState* state) {
    if (!g_hks_lua_pushnumber) {
        return 0;
    }
    push_number(state, g_dash_button_is_left_stick_click);
    return 1;
}

int lua_ERSplit_IsDashHeldNative(HksState* state) {
    if (!g_hks_lua_pushnumber) {
        return 0;
    }
    int pad_index = -1;
    const bool held = query_keyboard_held() || query_button_held(pad_index);
    push_number(state, held);
    return 1;
}

void register_native_global(HksState* state) {
    if (!g_hks_addnamedcclosure) {
        return;
    }
    struct native_entry {
        const char* name;
        void* func;
    };
    const native_entry natives[] = {
        { kNativeDashHeld, reinterpret_cast<void*>(&lua_ERSplit_IsDashHeldNative) },
        { kNativeDashKeyHeld, reinterpret_cast<void*>(&lua_ERSplit_IsDashKeyHeldNative) },
        { kNativeDashButtonHeld, reinterpret_cast<void*>(&lua_ERSplit_IsDashButtonHeldNative) },
        { kNativeDashLeftStickClick,
            reinterpret_cast<void*>(&lua_ERSplit_IsDashLeftStickClickNative) },
        { kNativeDashButtonNeutralHeld,
            reinterpret_cast<void*>(&lua_ERSplit_IsDashButtonNeutralHeldNative) },
        { kNativeDashButtonConfigured,
            reinterpret_cast<void*>(&lua_ERSplit_IsDashButtonConfiguredNative) },
        { kNativeDashButtonPressed,
            reinterpret_cast<void*>(&lua_ERSplit_IsDashButtonPressedNative) },
    };
    for (const auto& entry : natives) {
        g_hks_addnamedcclosure(state, entry.name, entry.func);
    }
    if (!g_hks_registration_logged.exchange(true)) {
        append_log("hks_global_registration=ok "
                   "names=ERSplit_IsDashHeldNative,ERSplit_IsDashKeyHeldNative,"
                   "ERSplit_IsDashButtonHeldNative,ERSplit_IsDashButtonNeutralHeldNative,"
                   "ERSplit_IsDashButtonConfiguredNative,ERSplit_IsDashButtonPressedNative,"
                   "ERSplit_IsDashLeftStickClickNative");
    }
}

void hks_set_cglobals_hook(HksState* state) {
    if (g_hks_set_cglobals_original) {
        g_hks_set_cglobals_original(state);
    }
    register_native_global(state);
}

bool resolve_hks_api(std::ostringstream& log) {
    const auto module = hks_pattern_scan::get_main_module();
    if (!module.text_start) {
        log << "hks_scan=.text_missing\n";
        return false;
    }

    const char* create_hks_state_aob =
        "48 8b cb e8 ?? ?? ?? ?? 48 8b cb e8 ?? ?? ?? ?? 48 8b cb e8 ?? ?? ?? ?? "
        "48 8b cb e8 ?? ?? ?? ?? 48 8b cb e8 ?? ?? ?? ?? 48 8b cb e8 ?? ?? ?? ?? "
        "48 8b c3";
    const char* hks_addnamedcclosure_aob =
        "48 89 5c 24 08 57 48 83 ec 30 49 8b c0 c7 44 24 20 00 00 00 00 48 8b da "
        "4c 8b ca 48 8b d0 45 33 c0 48 8b f9";
    const char* hks_lua_pushnumber_aob =
        "48 8b 41 48 f3 0f 11 48 08 c7 00 03 00 00 00 48 83 c0 10 48 89 41 48 c3";

    g_hks_lua_pushnumber = reinterpret_cast<hks_lua_pushnumber_fn>(
        hks_pattern_scan::scan_code(module, hks_lua_pushnumber_aob));
    g_hks_addnamedcclosure = reinterpret_cast<hks_addnamedcclosure_fn>(
        hks_pattern_scan::scan_code(module, hks_addnamedcclosure_aob));
    g_hks_set_cglobals = reinterpret_cast<hks_set_cglobals_fn>(
        hks_pattern_scan::scan_code_call(module, create_hks_state_aob, 43, 43));

    log << "hks_lua_pushnumber=" << g_hks_lua_pushnumber << '\n'
        << "hks_addnamedcclosure=" << g_hks_addnamedcclosure << '\n'
        << "hksSetCGlobals=" << g_hks_set_cglobals << '\n';

    return g_hks_lua_pushnumber && g_hks_addnamedcclosure && g_hks_set_cglobals;
}

bool install_hks_hook(std::ostringstream& log) {
    if (MH_Initialize() != MH_OK) {
        log << "minhook_init=fail\n";
        return false;
    }

    if (MH_CreateHook(reinterpret_cast<LPVOID>(g_hks_set_cglobals),
            reinterpret_cast<LPVOID>(&hks_set_cglobals_hook),
            reinterpret_cast<LPVOID*>(&g_hks_set_cglobals_original))
        != MH_OK) {
        log << "minhook_create_hksSetCGlobals=fail\n";
        MH_Uninitialize();
        return false;
    }

    if (MH_EnableHook(reinterpret_cast<LPVOID>(g_hks_set_cglobals)) != MH_OK) {
        log << "minhook_enable_hksSetCGlobals=fail\n";
        MH_RemoveHook(reinterpret_cast<LPVOID>(g_hks_set_cglobals));
        MH_Uninitialize();
        return false;
    }

    log << "minhook_hksSetCGlobals=ok\n";
    return true;
}

DWORD WINAPI bridge_thread(LPVOID param) {
    const auto bridge_module = static_cast<HMODULE>(param);
    std::string config_dir;
    g_ini_path = bridge_config::resolve_ini_path(bridge_module, config_dir);
    g_log_path = bridge_config::join_path(config_dir, bridge_config::kLogName);

    std::ostringstream boot;
    boot << "separate_roll_and_sprint (libER-independent)\n"
         << "libER_runtime_dependency=none\n"
         << "config_dir=" << config_dir << '\n'
         << "log=" << g_log_path << '\n'
         << "ini=" << g_ini_path << '\n'
         << "hotkey: F10=re-read INI\n";

    log_xinput_boot_line(boot);

    const bridge_config::ini_config cfg = bridge_config::read_ini(g_ini_path);
    load_dash_config_from_ini(boot, cfg);

    if (!resolve_hks_api(boot)) {
        boot << "hks_global_registration=fail reason=hks_api_scan\n";
        write_log_header(boot.str());
        return 0;
    }

    if (!install_hks_hook(boot)) {
        boot << "hks_global_registration=fail reason=minhook\n";
        write_log_header(boot.str());
        return 0;
    }

    boot << "hks_global_registration=pending\n"
         << "native_globals=" << kNativeDashHeld << "," << kNativeDashKeyHeld << ","
         << kNativeDashButtonHeld << "," << kNativeDashButtonNeutralHeld << ","
         << kNativeDashButtonConfigured << "," << kNativeDashButtonPressed << ","
         << kNativeDashLeftStickClick << '\n'
         << "poll=keyboard GetAsyncKeyState + XInput dash_button\n"
         << "startup_checkpoint_complete sprint_only=1\n";

    write_log_header(boot.str());

    for (;;) {
        if (poll_hotkey_edge(kHotkeyReloadIni, g_hotkey_f10_down)) {
            reload_ini_hotkey();
        }
        log_input_transitions();
        Sleep(16);
    }
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE thread =
            CreateThread(nullptr, 0, bridge_thread, instance, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}

// Sprint-only dash bridge: INI dash_key + HKS ERSplit_IsDashHeldNative.
// No runtime dependency on libER.dll (links MinHook only).

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <MinHook.h>

#include "bridge_config.hpp"
#include "dash_key_parse.hpp"
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

constexpr const char* kNativeGlobalName = "ERSplit_IsDashHeldNative";
constexpr int kHotkeyReloadIni = VK_F10;

std::mutex g_log_mutex;
std::string g_log_path;
std::string g_ini_path;

std::atomic<int> g_last_logged_held{ -1 };
std::atomic<bool> g_hks_registration_logged{ false };
std::atomic<bool> g_hotkey_f10_down{ false };

er_dash_key_parse::parsed_dash_key g_active_dash_key{};

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

bool load_dash_key_from_ini(std::ostringstream& boot, const bridge_config::ini_config& cfg) {
    if (cfg.enable_menu_patch) {
        boot << "enable_menu_patch=1 ignored by this DLL\n"
             << "menu_patch_note=optional libER menu diagnostic is not part of this "
                "shipped bridge\n";
    }

    const auto parsed = bridge_config::parse_active_dash_key(cfg);
    if (!parsed || !parsed->ok) {
        if (cfg.has_dash_key) {
            boot << "config_error unknown_dash_key=\"" << cfg.dash_key << "\"\n"
                 << "supported_keys=" << er_dash_key_parse::kSupportedKeysList << '\n';
        }
        boot << "config_loaded dash_key=none\n";
        return false;
    }

    g_active_dash_key = *parsed;
    boot << "config_loaded dash_key=" << g_active_dash_key.normalized_name
         << " vk=" << format_vk_hex(g_active_dash_key.primary_vk) << '\n';
    boot << "sprint_source=ini dash_key\n";
    return true;
}

bool query_dash_held() {
    if (!g_active_dash_key.ok) {
        return false;
    }
    return er_dash_key_parse::query_held(g_active_dash_key);
}

void log_held_change_if_needed(bool held) {
    const int held_i = held ? 1 : 0;
    const int prev = g_last_logged_held.exchange(held_i);
    if (prev == held_i) {
        return;
    }
    std::ostringstream line;
    line << "native_dash_held transition=" << (held ? "down" : "up")
         << " dash_key=" << g_active_dash_key.normalized_name
         << " vk=" << format_vk_hex(g_active_dash_key.primary_vk);
    append_log(line.str());
}

bool poll_hotkey_edge(int vk, std::atomic<bool>& was_down) {
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool prev = was_down.exchange(down);
    return down && !prev;
}

void reload_ini_hotkey() {
    std::ostringstream out;
    out << "\n========== F10 INI reload ==========\n";
    const bridge_config::ini_config cfg = bridge_config::read_ini(g_ini_path);
    load_dash_key_from_ini(out, cfg);
    out << "libER_runtime_dependency=none\n";
    append_log(out.str());
}

int lua_ERSplit_IsDashHeldNative(HksState* state) {
    if (!g_hks_lua_pushnumber) {
        return 0;
    }
    const bool held = query_dash_held();
    log_held_change_if_needed(held);
    g_hks_lua_pushnumber(state, held ? 1.f : 0.f);
    return 1;
}

void register_native_global(HksState* state) {
    if (!g_hks_addnamedcclosure) {
        return;
    }
    g_hks_addnamedcclosure(state, kNativeGlobalName,
        reinterpret_cast<void*>(&lua_ERSplit_IsDashHeldNative));
    if (!g_hks_registration_logged.exchange(true)) {
        append_log("hks_global_registration=ok name=ERSplit_IsDashHeldNative");
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
         << "hotkey: F10=re-read INI dash_key\n";

    const bridge_config::ini_config cfg = bridge_config::read_ini(g_ini_path);
    load_dash_key_from_ini(boot, cfg);

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
         << "native_global=" << kNativeGlobalName << '\n'
         << "poll=GetAsyncKeyState per dash_key alias rules\n"
         << "startup_checkpoint_complete sprint_only=1\n";

    write_log_header(boot.str());

    for (;;) {
        if (poll_hotkey_edge(kHotkeyReloadIni, g_hotkey_f10_down)) {
            reload_ini_hotkey();
        }
        log_held_change_if_needed(query_dash_held());
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

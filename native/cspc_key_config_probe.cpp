// Read-only probe: GLOBAL_CSPcKeyConfig bounded window scan for live movement binds.
// Requires libER.dll + this DLL. No hooks, no writes, no broad process scan.

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <detail/base_address.hpp>
#include <detail/symbols.hpp>

#include "bridge_config.hpp"
#include "cspc_key_config_live_binds.hpp"
#include "cspc_key_config_window_scan.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr const char* kLogName = "libER_cspc_key_config_probe.log";
constexpr const char* kIniName = "libER_cspc_key_config_probe.ini";

constexpr int kHotkeySnapshot = VK_F8;
constexpr int kHotkeyRescan = VK_F9;
constexpr int kHotkeySummary = VK_F10;

constexpr size_t kDefaultWindow = 0x4000;
constexpr size_t kExpandedWindow = 0x10000;
constexpr size_t kMaxHits = 4096;
constexpr size_t kMaxLogLines = 512;

struct probe_ini_config {
    size_t initial_window{ kDefaultWindow };
    size_t expanded_window{ kExpandedWindow };
    size_t max_hits{ kMaxHits };
};

struct symbol_probe {
    const char* name;
    uintptr_t rva{ 0 };
    uintptr_t slot_address{ 0 };
    void* slot_pointer{ nullptr };
    bool slot_readable{ false };
};

std::mutex g_log_mutex;
std::string g_log_path;
std::string g_ini_path;
probe_ini_config g_cfg{};

std::vector<cspc_key_config_window_scan::snapshot_entry> g_snapshot;
uintptr_t g_snapshot_instance{ 0 };
size_t g_snapshot_window{ 0 };
bool g_has_snapshot{ false };
std::array<cspc_key_config_live_binds::movement_bind_sample, 4> g_layout_snapshot{};
bool g_has_layout_snapshot{ false };

std::atomic<bool> g_hotkey_f8{ false };
std::atomic<bool> g_hotkey_f9{ false };
std::atomic<bool> g_hotkey_f10{ false };

std::string resolve_log_path(HMODULE module) {
    char bind_dir_env[MAX_PATH]{};
    if (GetEnvironmentVariableA(bridge_config::kBindDirEnv, bind_dir_env, MAX_PATH) > 0) {
        return bridge_config::join_path(bind_dir_env, kLogName);
    }
    const std::string me3_bind = bridge_config::me3_bind_dir();
    if (!me3_bind.empty()) {
        return bridge_config::join_path(me3_bind, kLogName);
    }
    const std::string dll_dir = bridge_config::module_dir(module);
    if (!dll_dir.empty()) {
        return bridge_config::join_path(dll_dir, kLogName);
    }
    return kLogName;
}

std::string resolve_ini_path(HMODULE module) {
    char bind_dir_env[MAX_PATH]{};
    if (GetEnvironmentVariableA(bridge_config::kBindDirEnv, bind_dir_env, MAX_PATH) > 0) {
        return bridge_config::join_path(bind_dir_env, kIniName);
    }
    const std::string me3_bind = bridge_config::me3_bind_dir();
    if (!me3_bind.empty()) {
        return bridge_config::join_path(me3_bind, kIniName);
    }
    const std::string dll_dir = bridge_config::module_dir(module);
    if (!dll_dir.empty()) {
        return bridge_config::join_path(dll_dir, kIniName);
    }
    return kIniName;
}

size_t parse_size_value(const std::string& text, size_t fallback) {
    if (text.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 0);
    if (end == text.c_str()) {
        return fallback;
    }
    return static_cast<size_t>(parsed);
}

probe_ini_config read_probe_ini(const std::string& ini_path) {
    probe_ini_config cfg{};
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
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        const auto trim = [](std::string& s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
                s.pop_back();
            }
            size_t start = 0;
            while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
                ++start;
            }
            s = s.substr(start);
        };
        trim(key);
        trim(value);

        if (key == "initial_window") {
            cfg.initial_window = parse_size_value(value, kDefaultWindow);
        } else if (key == "expanded_window") {
            cfg.expanded_window = parse_size_value(value, kExpandedWindow);
        } else if (key == "max_hits") {
            cfg.max_hits = parse_size_value(value, kMaxHits);
        }
    }
    return cfg;
}

void append_log_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::ofstream out(g_log_path, std::ios::out | std::ios::app);
    if (!out) {
        return;
    }
    out << line << '\n';
}

template <liber::sym_table::sym_key Name>
uintptr_t symbol_slot_address() {
    return reinterpret_cast<uintptr_t>(
        liber::base_address::const_offset<liber::symbol<Name>::get()>());
}

template <liber::sym_table::sym_key Name>
symbol_probe make_symbol_probe(const char* name) {
    symbol_probe probe{};
    probe.name = name;
    probe.rva = liber::symbol<Name>::get();
    probe.slot_address = symbol_slot_address<Name>();
    MEMORY_BASIC_INFORMATION mbi{};
    probe.slot_readable = cspc_key_config_window_scan::query_region(probe.slot_address, mbi)
        && mbi.State == MEM_COMMIT
        && cspc_key_config_window_scan::is_readable_protect(mbi.Protect);
    if (probe.slot_readable) {
        cspc_key_config_window_scan::safe_read_bytes(
            probe.slot_address, &probe.slot_pointer, sizeof(probe.slot_pointer));
    }
    return probe;
}

void log_symbol_probes(const char* tag) {
    const symbol_probe cspc =
        make_symbol_probe<"GLOBAL_CSPcKeyConfig">("GLOBAL_CSPcKeyConfig");
    const symbol_probe user =
        make_symbol_probe<"GLOBAL_UserConfig">("GLOBAL_UserConfig");
    const symbol_probe pad =
        make_symbol_probe<"GLOBAL_FD4PadManager">("GLOBAL_FD4PadManager");

    std::ostringstream out;
    out << tag << " symbol_refs\n";
    for (const auto* probe : { &cspc, &user, &pad }) {
        out << "  " << probe->name << " rva=0x" << std::hex << probe->rva << std::dec
            << " slot=0x" << std::hex << probe->slot_address << std::dec
            << " slot_readable=" << (probe->slot_readable ? 1 : 0)
            << " *slot=0x" << std::hex
            << reinterpret_cast<uintptr_t>(probe->slot_pointer) << std::dec << '\n';
    }
    append_log_line(out.str());
}

bool poll_hotkey_edge(int vk, std::atomic<bool>& was_down) {
    const SHORT state = GetAsyncKeyState(vk);
    const bool down = (state & 0x8000) != 0;
    const bool prev = was_down.exchange(down);
    return down && !prev;
}

bool resolve_cspc_instance(uintptr_t& instance_out, std::string& detail_out) {
    const auto probe = make_symbol_probe<"GLOBAL_CSPcKeyConfig">("GLOBAL_CSPcKeyConfig");
    if (!probe.slot_readable) {
        detail_out = "GLOBAL_CSPcKeyConfig slot not readable";
        return false;
    }
    void* instance = nullptr;
    if (!cspc_key_config_window_scan::safe_read_bytes(
            probe.slot_address, &instance, sizeof(instance))) {
        detail_out = "failed to read GLOBAL_CSPcKeyConfig slot pointer";
        return false;
    }
    instance_out = reinterpret_cast<uintptr_t>(instance);
    if (instance_out == 0) {
        detail_out = "GLOBAL_CSPcKeyConfig instance is null";
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (!cspc_key_config_window_scan::can_read_range(instance_out, 16, mbi)) {
        detail_out = "GLOBAL_CSPcKeyConfig instance not readable";
        return false;
    }
    std::ostringstream detail;
    detail << "instance=0x" << std::hex << instance_out << std::dec
           << " slot=0x" << std::hex << probe.slot_address << std::dec;
    detail_out = detail.str();
    return true;
}

void snapshot_movement_layout(uintptr_t instance, const char* tag) {
    g_layout_snapshot = cspc_key_config_live_binds::read_all_direction_binds(instance);
    g_has_layout_snapshot = true;

    std::ostringstream out;
    cspc_key_config_live_binds::dump_movement_layout_block(out, instance, tag);
    append_log_line(out.str());
}

void log_movement_layout_diff(uintptr_t instance) {
    if (!g_has_layout_snapshot) {
        append_log_line("movement_layout_diff skipped (no layout snapshot)");
        return;
    }

    const auto current = cspc_key_config_live_binds::read_all_direction_binds(instance);
    for (size_t i = 0; i < 4; ++i) {
        const auto& dir = cspc_key_config_live_binds::kDirections[i];
        const auto& old_sample = g_layout_snapshot[i];
        const auto& new_sample = current[i];
        if (!old_sample.valid || !new_sample.valid) {
            continue;
        }
        if (old_sample.er_id == new_sample.er_id) {
            continue;
        }

        std::ostringstream line;
        line << "movement_live_bind direction=" << dir.name << " offset=0x" << std::hex
             << dir.offset << std::dec << " old_er=" << old_sample.er_id
             << " new_er=" << new_sample.er_id << " old_vk="
             << cspc_key_config_live_binds::vk_to_bind_label(old_sample.vk) << " new_vk="
             << cspc_key_config_live_binds::vk_to_bind_label(new_sample.vk);
        append_log_line(line.str());
    }
}

void on_f8_snapshot() {
    uintptr_t instance = 0;
    std::string detail;
    if (!resolve_cspc_instance(instance, detail)) {
        append_log_line(std::string("F8_SNAPSHOT failed: ") + detail);
        return;
    }

    snapshot_movement_layout(instance, "F8_SNAPSHOT");

    g_snapshot = cspc_key_config_window_scan::scan_instance_window(
        instance, g_cfg.initial_window, g_cfg.max_hits);
    g_snapshot_instance = instance;
    g_snapshot_window = g_cfg.initial_window;
    g_has_snapshot = true;

    std::ostringstream out;
    out << "F8_SNAPSHOT " << detail << " window=0x" << std::hex << g_cfg.initial_window
        << std::dec << '\n';
    cspc_key_config_window_scan::dump_hits(out, g_snapshot, kMaxLogLines);
    append_log_line(out.str());
}

void log_expanded_scan(uintptr_t instance, const char* reason) {
    const auto expanded_hits = cspc_key_config_window_scan::scan_instance_window(
        instance, g_cfg.expanded_window, g_cfg.max_hits);
    std::ostringstream out;
    out << "EXPANDED_SCAN reason=" << reason << " instance=0x" << std::hex << instance
        << std::dec << " window=0x" << std::hex << g_cfg.expanded_window << std::dec << '\n';
    cspc_key_config_window_scan::dump_hits(out, expanded_hits, kMaxLogLines);
    append_log_line(out.str());
}

void on_f9_rescan() {
    if (!g_has_snapshot) {
        append_log_line("F9_RESCAN ignored (press F8 first)");
        return;
    }

    uintptr_t instance = 0;
    std::string detail;
    if (!resolve_cspc_instance(instance, detail)) {
        append_log_line(std::string("F9_RESCAN failed: ") + detail);
        return;
    }

    if (instance != g_snapshot_instance) {
        std::ostringstream moved;
        moved << "F9_WARNING instance moved: snapshot=0x" << std::hex << g_snapshot_instance
              << " current=0x" << instance << std::dec;
        append_log_line(moved.str());
    }

    {
        std::ostringstream layout_out;
        cspc_key_config_live_binds::dump_movement_layout_block(
            layout_out, instance, "F9_RESCAN");
        append_log_line(layout_out.str());
    }
    log_movement_layout_diff(instance);

    const auto changes =
        cspc_key_config_window_scan::diff_snapshot(g_snapshot, instance);

    std::ostringstream out;
    out << "F9_RESCAN " << detail << " window=0x" << std::hex << g_snapshot_window << std::dec
        << '\n';
    cspc_key_config_window_scan::dump_diff(out, changes);

    size_t rebind_hits = 0;
    for (const auto& change : changes) {
        if (change.rebind_move_back) {
            ++rebind_hits;
        }
    }
    out << "rebind_move_back_hits=" << rebind_hits << '\n';
    append_log_line(out.str());

    if (changes.empty() && g_snapshot_window < g_cfg.expanded_window) {
        append_log_line("F9_NO_CHANGES expanding scan window");
        log_expanded_scan(instance, "no_changes_at_initial_window");
    }
}

void dump_summary() {
    std::ostringstream out;
    out << "\n========== F10 SUMMARY ==========\n";
    out << "has_snapshot=" << (g_has_snapshot ? "1" : "0") << '\n';
    if (g_has_snapshot) {
        out << "snapshot_instance=0x" << std::hex << g_snapshot_instance << std::dec
            << " snapshot_window=0x" << std::hex << g_snapshot_window << std::dec
            << " hit_count=" << g_snapshot.size() << '\n';
    }
    out << "initial_window=0x" << std::hex << g_cfg.initial_window << std::dec
        << " expanded_window=0x" << std::hex << g_cfg.expanded_window << std::dec << '\n';
    out << "\nMovement layout offsets:\n"
        << "  forward=0x458 back=0x46c left=0x480 right=0x494\n";
    out << "\nTest flow (one rebind at a time):\n"
        << "  F8 snapshot -> rebind one direction -> apply -> F9\n"
        << "  Forward W->I  Back S->J  Left A->K  Right D->L\n"
        << "  Expect movement_live_bind lines with old_er/new_er and old_vk/new_vk\n";
    out << "\nMovement defaults:\n"
        << "  er_id forward=86 back=100 left=99 right=101\n"
        << "  rebind er_id forward=92 back=105 left=106 right=107\n";
    out << "KeyAssignParam rows 1-4 are display-only.\n";
    out << "========== END SUMMARY ==========\n";
    append_log_line(out.str());
}

DWORD WINAPI probe_thread(LPVOID param) {
    const auto module = static_cast<HMODULE>(param);
    g_log_path = resolve_log_path(module);
    g_ini_path = resolve_ini_path(module);
    g_cfg = read_probe_ini(g_ini_path);

    {
        std::ostringstream boot;
        boot << "libER_cspc_key_config_probe (read-only)\n"
             << "log=" << g_log_path << '\n'
             << "ini=" << g_ini_path << '\n'
             << "initial_window=0x" << std::hex << g_cfg.initial_window << std::dec
             << " expanded_window=0x" << std::hex << g_cfg.expanded_window << std::dec
             << " max_hits=" << g_cfg.max_hits << '\n'
             << "load_order=libER.dll then this DLL\n"
             << "no separate_roll_and_sprint.dll — no HKS — no hooks\n"
             << "\nHotkeys:\n"
             << "  F8 = snapshot CSPcKeyConfig window\n"
             << "  F9 = rescan + diff (auto-expand if no changes)\n"
             << "  F10 = summary\n";
        append_log_line(boot.str());
    }

    log_symbol_probes("startup");
    {
        uintptr_t instance = 0;
        std::string detail;
        if (resolve_cspc_instance(instance, detail)) {
            snapshot_movement_layout(instance, "startup");
        } else {
            append_log_line(std::string("startup movement_layout_block unavailable: ") + detail);
        }
    }

    for (;;) {
        if (poll_hotkey_edge(kHotkeySnapshot, g_hotkey_f8)) {
            log_symbol_probes("F8");
            on_f8_snapshot();
        }
        if (poll_hotkey_edge(kHotkeyRescan, g_hotkey_f9)) {
            log_symbol_probes("F9");
            on_f9_rescan();
        }
        if (poll_hotkey_edge(kHotkeySummary, g_hotkey_f10)) {
            dump_summary();
        }
        Sleep(16);
    }
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, probe_thread, instance, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}

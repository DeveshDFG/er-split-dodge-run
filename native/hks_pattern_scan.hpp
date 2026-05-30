#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace hks_pattern_scan {

struct module_view {
    uintptr_t base{ 0 };
    size_t size{ 0 };
    uintptr_t text_start{ 0 };
    size_t text_size{ 0 };
};

struct pattern_byte {
    uint8_t value{ 0 };
    bool wildcard{ false };
};

inline module_view get_main_module() {
    module_view view{};
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    if (!base) {
        return view;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    view.base = reinterpret_cast<uintptr_t>(base);
    view.size = nt->OptionalHeader.SizeOfImage;
    const auto& section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (std::memcmp(section[i].Name, ".text", 5) == 0) {
            view.text_start = view.base + section[i].VirtualAddress;
            view.text_size = section[i].Misc.VirtualSize;
            break;
        }
    }
    return view;
}

inline std::vector<pattern_byte> parse_ida_pattern(const char* pattern) {
    std::vector<pattern_byte> bytes;
    std::istringstream stream(pattern);
    std::string token;
    while (stream >> token) {
        pattern_byte entry{};
        if (token == "?" || token == "??") {
            entry.wildcard = true;
        } else {
            entry.value = static_cast<uint8_t>(std::stoul(token, nullptr, 16));
        }
        bytes.push_back(entry);
    }
    return bytes;
}

inline uint8_t* scan(const module_view& module,
    const std::vector<pattern_byte>& pattern) {
    if (!module.text_start || pattern.empty()) {
        return nullptr;
    }
    const size_t limit = module.text_size - pattern.size();
    for (size_t i = 0; i < limit; ++i) {
        auto* candidate = reinterpret_cast<uint8_t*>(module.text_start + i);
        bool match = true;
        for (size_t j = 0; j < pattern.size(); ++j) {
            if (pattern[j].wildcard) {
                continue;
            }
            if (candidate[j] != pattern[j].value) {
                match = false;
                break;
            }
        }
        if (match) {
            return candidate;
        }
    }
    return nullptr;
}

inline uint8_t* scan_code(const module_view& module, const char* pattern) {
    return scan(module, parse_ida_pattern(pattern));
}

inline void* scan_code_call(const module_view& module, const char* pattern,
    int func_start_to_op, int op_offset) {
    uint8_t* insn = scan_code(module, pattern);
    if (!insn) {
        return nullptr;
    }
    const int32_t rel =
        *reinterpret_cast<int32_t*>(insn + op_offset + 1);
    return insn + rel + 5 + func_start_to_op;
}

} // namespace hks_pattern_scan

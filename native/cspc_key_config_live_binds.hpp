#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "cspc_key_config_symbols.generated.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>

namespace cspc_key_config_live_binds {

inline constexpr std::uintptr_t kGlobalCspcKeyConfigRva =
    cspc_key_config_symbols::kGlobalCspcKeyConfigRva;

inline constexpr std::uintptr_t kOffsetMoveForward = 0x458;
inline constexpr std::uintptr_t kOffsetMoveBack = 0x46c;
inline constexpr std::uintptr_t kOffsetMoveLeft = 0x480;
inline constexpr std::uintptr_t kOffsetMoveRight = 0x494;

inline constexpr int kMinErKeyboardId = 1;
inline constexpr int kMaxErKeyboardId = 255;
inline constexpr int kErIdToScanOffset = 69;

struct direction_layout {
    const char* name;
    std::uintptr_t offset;
};

constexpr direction_layout kDirections[] = {
    { "forward", kOffsetMoveForward },
    { "back", kOffsetMoveBack },
    { "left", kOffsetMoveLeft },
    { "right", kOffsetMoveRight },
};

inline bool is_readable_protect(DWORD protect) {
    if (protect & PAGE_GUARD) {
        return false;
    }
    const DWORD base = protect & 0xFF;
    if (base == PAGE_NOACCESS) {
        return false;
    }
    return base == PAGE_READONLY || base == PAGE_READWRITE || base == PAGE_WRITECOPY
        || base == PAGE_EXECUTE_READ || base == PAGE_EXECUTE_READWRITE
        || base == PAGE_EXECUTE_WRITECOPY;
}

inline bool can_read_range(std::uintptr_t address, size_t size, MEMORY_BASIC_INFORMATION& mbi) {
    if (size == 0) {
        return false;
    }
    if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) != sizeof(mbi)) {
        return false;
    }
    if (mbi.State != MEM_COMMIT || !is_readable_protect(mbi.Protect)) {
        return false;
    }
    const std::uintptr_t region_base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    const std::uintptr_t region_end = region_base + mbi.RegionSize;
    const std::uintptr_t end = address + size;
    return address >= region_base && end >= address && end <= region_end;
}

inline bool safe_read_bytes(std::uintptr_t address, void* out, size_t size) {
    if (!out || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (!can_read_range(address, size, mbi)) {
        return false;
    }
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline std::uintptr_t game_image_base() {
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
}

inline std::uintptr_t global_cspc_key_config_slot_address() {
    return game_image_base() + kGlobalCspcKeyConfigRva;
}

inline bool is_valid_er_keyboard_id(int er_id) {
    return er_id >= kMinErKeyboardId && er_id <= kMaxErKeyboardId;
}

inline std::optional<int> er_keyboard_id_to_vk(int er_id) {
    if (!is_valid_er_keyboard_id(er_id)) {
        return std::nullopt;
    }
    const int scan_code = er_id - kErIdToScanOffset;
    if (scan_code <= 0) {
        return std::nullopt;
    }
    const UINT vk = MapVirtualKeyA(static_cast<UINT>(scan_code), MAPVK_VSC_TO_VK_EX);
    if (vk == 0) {
        return std::nullopt;
    }
    return static_cast<int>(vk);
}

inline std::optional<int32_t> read_movement_er_id(std::uintptr_t instance, std::uintptr_t offset) {
    int32_t er_id = 0;
    const std::uintptr_t address = instance + offset;
    if (!safe_read_bytes(address, &er_id, sizeof(er_id))) {
        return std::nullopt;
    }
    if (!is_valid_er_keyboard_id(er_id)) {
        return std::nullopt;
    }
    return er_id;
}

inline bool resolve_instance(std::uintptr_t& instance_out) {
    void* instance = nullptr;
    const std::uintptr_t slot = global_cspc_key_config_slot_address();
    if (!safe_read_bytes(slot, &instance, sizeof(instance))) {
        return false;
    }
    instance_out = reinterpret_cast<std::uintptr_t>(instance);
    return instance_out != 0;
}

inline std::string vk_to_bind_label(int vk) {
    if (vk >= 'A' && vk <= 'Z') {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= '0' && vk <= '9') {
        return std::string(1, static_cast<char>(vk));
    }
    switch (vk) {
    case VK_SPACE:
        return "Space";
    case VK_LSHIFT:
        return "LeftShift";
    case VK_RSHIFT:
        return "RightShift";
    case VK_LCONTROL:
        return "LeftCtrl";
    case VK_RCONTROL:
        return "RightCtrl";
    case VK_LMENU:
        return "LeftAlt";
    case VK_RMENU:
        return "RightAlt";
    default:
        break;
    }
    std::ostringstream out;
    out << "0x" << std::hex << vk << std::dec;
    return out.str();
}

inline std::string format_scan_code(int er_id) {
    const int scan = er_id - kErIdToScanOffset;
    if (scan <= 0) {
        return "n/a";
    }
    std::ostringstream out;
    out << "0x" << std::hex << scan << std::dec;
    return out.str();
}

struct movement_bind_sample {
    bool valid{ false };
    int er_id{ -1 };
    int vk{ -1 };
    int32_t raw_int32{ 0 };
};

inline movement_bind_sample read_direction_bind(std::uintptr_t instance,
    const direction_layout& dir) {
    movement_bind_sample sample{};
    const auto er_id = read_movement_er_id(instance, dir.offset);
    if (!er_id) {
        return sample;
    }
    sample.raw_int32 = *er_id;
    const auto vk = er_keyboard_id_to_vk(*er_id);
    if (!vk) {
        return sample;
    }
    sample.valid = true;
    sample.er_id = *er_id;
    sample.vk = *vk;
    return sample;
}

inline std::array<movement_bind_sample, 4> read_all_direction_binds(std::uintptr_t instance) {
    std::array<movement_bind_sample, 4> out{};
    for (size_t i = 0; i < 4; ++i) {
        out[i] = read_direction_bind(instance, kDirections[i]);
    }
    return out;
}

inline bool all_direction_binds_valid(const std::array<movement_bind_sample, 4>& samples) {
    for (const auto& sample : samples) {
        if (!sample.valid) {
            return false;
        }
    }
    return true;
}

struct typed_field_read {
    bool readable{ false };
    uint8_t byte_value{ 0 };
    uint16_t int16_value{ 0 };
    uint32_t int32_value{ 0 };
};

inline typed_field_read read_typed_field(std::uintptr_t instance, std::uintptr_t offset) {
    typed_field_read out{};
    const std::uintptr_t address = instance + offset;
    out.readable = safe_read_bytes(address, &out.byte_value, sizeof(out.byte_value))
        && safe_read_bytes(address, &out.int16_value, sizeof(out.int16_value))
        && safe_read_bytes(address, &out.int32_value, sizeof(out.int32_value));
    return out;
}

inline void append_layout_line(std::ostream& out, const direction_layout& dir,
    std::uintptr_t instance) {
    const typed_field_read field = read_typed_field(instance, dir.offset);
    const auto er_id = read_movement_er_id(instance, dir.offset);
    const auto vk = er_id ? er_keyboard_id_to_vk(*er_id) : std::nullopt;

    out << "  " << dir.name << " offset=0x" << std::hex << dir.offset << std::dec
        << " readable=" << (field.readable ? 1 : 0) << " byte=" << static_cast<int>(field.byte_value)
        << " int16=" << field.int16_value << " int32=" << field.int32_value;
    if (er_id) {
        out << " er_id=" << *er_id << " scan=" << format_scan_code(*er_id);
    }
    if (vk) {
        out << " vk=" << vk_to_bind_label(*vk);
    }
    out << '\n';
}

inline void dump_movement_layout_block(std::ostream& out, std::uintptr_t instance,
    const char* tag) {
    out << tag << " movement_layout_block instance=0x" << std::hex << instance << std::dec
        << " cspc_rva=0x" << std::hex << kGlobalCspcKeyConfigRva << std::dec << '\n';
    for (const auto& dir : kDirections) {
        append_layout_line(out, dir, instance);
    }
}

struct resolved_live_binds {
    bool valid{ false };
    std::uintptr_t instance{ 0 };
    std::array<movement_bind_sample, 4> samples{};
};

inline std::optional<resolved_live_binds> try_resolve_live_binds() {
    std::uintptr_t instance = 0;
    if (!resolve_instance(instance)) {
        return std::nullopt;
    }

    resolved_live_binds out{};
    out.instance = instance;
    out.samples = read_all_direction_binds(instance);
    if (!all_direction_binds_valid(out.samples)) {
        return std::nullopt;
    }
    out.valid = true;
    return out;
}

} // namespace cspc_key_config_live_binds

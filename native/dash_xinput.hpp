#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <XInput.h>

#include <string>

namespace dash_xinput {

using xinput_get_state_fn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

inline xinput_get_state_fn g_get_state = nullptr;
inline HMODULE g_module = nullptr;
inline std::string g_dll_name;
inline bool g_available = false;

inline bool init(std::string* dll_name_out = nullptr) {
    if (g_available && g_get_state) {
        if (dll_name_out) {
            *dll_name_out = g_dll_name;
        }
        return true;
    }

    const char* dll_candidates[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    for (const char* dll_name : dll_candidates) {
        HMODULE module = LoadLibraryA(dll_name);
        if (!module) {
            continue;
        }
        auto* get_state = reinterpret_cast<xinput_get_state_fn>(
            GetProcAddress(module, "XInputGetState"));
        if (!get_state) {
            FreeLibrary(module);
            continue;
        }
        g_module = module;
        g_get_state = get_state;
        g_dll_name = dll_name;
        g_available = true;
        if (dll_name_out) {
            *dll_name_out = g_dll_name;
        }
        return true;
    }

    g_available = false;
    g_get_state = nullptr;
    g_dll_name.clear();
    if (dll_name_out) {
        dll_name_out->clear();
    }
    return false;
}

inline bool available() {
    return g_available && g_get_state != nullptr;
}

inline bool read_pad_state(DWORD user_index, XINPUT_STATE& state_out) {
    if (!available()) {
        return false;
    }
    ZeroMemory(&state_out, sizeof(state_out));
    return g_get_state(user_index, &state_out) == ERROR_SUCCESS;
}

} // namespace dash_xinput

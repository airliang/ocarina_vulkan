#pragma once

#include "core/logging.h"
#include <cstdint>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#elif defined(__linux__)
#include <pthread.h>
#endif

namespace ocarina {

[[nodiscard]] inline uint64_t enki_task_thread_id() noexcept {
#if defined(_WIN32)
    return static_cast<uint64_t>(::GetCurrentThreadId());
#else
    return static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

// Sets the OS thread name visible in debuggers (e.g. Visual Studio Threads window).
inline void set_current_thread_name(const char* name) noexcept {
    if (name == nullptr || name[0] == '\0') {
        return;
    }

#if defined(_WIN32)
    using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);

    HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
    if (kernel32 != nullptr) {
        auto set_thread_description = reinterpret_cast<SetThreadDescriptionFn>(
            ::GetProcAddress(kernel32, "SetThreadDescription"));
        if (set_thread_description != nullptr) {
            wchar_t wide_name[128] = {};
            if (::MultiByteToWideChar(
                    CP_UTF8,
                    0,
                    name,
                    -1,
                    wide_name,
                    static_cast<int>(sizeof(wide_name) / sizeof(wide_name[0]))) > 0) {
                set_thread_description(::GetCurrentThread(), wide_name);
                return;
            }
        }
    }

    // Fallback for older Windows / debuggers: MSVC thread naming exception.
    #pragma pack(push, 8)
    struct ThreadNameInfo {
        DWORD type = 0x1000;
        LPCSTR name = nullptr;
        DWORD thread_id = 0;
        DWORD flags = 0;
    };
    #pragma pack(pop)

    ThreadNameInfo info{};
    info.name = name;
    info.thread_id = ::GetCurrentThreadId();
    __try {
        ::RaiseException(
            0x406D1388,
            0,
            sizeof(info) / sizeof(ULONG_PTR),
            reinterpret_cast<ULONG_PTR*>(&info));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#elif defined(__APPLE__)
    pthread_setname_np(name);
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), name);
#else
    (void)name;
#endif
}

inline void log_enki_task_execute(const char* task_name, uint32_t enki_thread_index, uint64_t& out_thread_id) noexcept {
    out_thread_id = enki_task_thread_id();
    OC_INFO_FORMAT(
        "{}::Execute enki_thread={} os_thread_id={}",
        task_name,
        enki_thread_index,
        out_thread_id);
}

inline void log_enki_task_execute_range(
    const char* task_name,
    uint32_t enki_thread_index,
    uint32_t range_begin,
    uint32_t range_end,
    uint64_t& out_thread_id) noexcept {
    out_thread_id = enki_task_thread_id();
    OC_INFO_FORMAT(
        "{}::ExecuteRange enki_thread={} range=[{}, {}) os_thread_id={}",
        task_name,
        enki_thread_index,
        range_begin,
        range_end,
        out_thread_id);
}

}// namespace ocarina

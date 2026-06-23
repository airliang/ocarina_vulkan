#pragma once

#include "core/logging.h"
#include <cstdint>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace ocarina {

[[nodiscard]] inline uint64_t enki_task_thread_id() noexcept {
#if defined(_WIN32)
    return static_cast<uint64_t>(::GetCurrentThreadId());
#else
    return static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
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

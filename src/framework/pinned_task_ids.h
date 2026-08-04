#pragma once

#include <cstdint>

namespace ocarina {

/// Thread indices for enki::IPinnedTask (must match TaskScheduler thread numbering).
/// Thread 0 is typically the thread that constructed the scheduler (main).
enum class PinnedTaskIds : uint32_t {
    RenderTask = 1,
    GPUResourceTask = 2,
};

[[nodiscard]] constexpr uint32_t pinned_task_thread_num(PinnedTaskIds id) noexcept {
    return static_cast<uint32_t>(id);
}

}// namespace ocarina

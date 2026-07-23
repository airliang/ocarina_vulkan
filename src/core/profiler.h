#pragma once

// Tracy profiler wrappers. No-ops when OCARINA_ENABLE_TRACY is not defined.
//
// Usage:
//   OC_PROFILE_FUNCTION;                  // current function scope
//   OC_PROFILE_SCOPE_N("MyScope");        // named scope
//   OC_PROFILE_FRAME_MARK;                // once per frame (e.g. end of render loop)
//   PROFILE_SCOPE();                      // legacy scope macro (Timer when Tracy is off)
//
// Connect with the Tracy desktop app while the game is running (Debug build recommended).

#if defined(OCARINA_ENABLE_TRACY) && OCARINA_ENABLE_TRACY
#include <tracy/Tracy.hpp>

#define OC_PROFILE_FUNCTION ZoneScoped
#define OC_PROFILE_SCOPE_N(name) ZoneScopedN(name)
#define OC_PROFILE_SCOPE_C(color) ZoneScopedC(color)
#define OC_PROFILE_FRAME_MARK FrameMark
#define OC_PROFILE_FRAME_MARK_N(name) FrameMarkNamed(name)
#define OC_PROFILE_PLOT(name, value) TracyPlot(name, value)
#define OC_PROFILE_PLOT_CONFIG(name, type) TracyPlotConfig(name, type, true, true, 0)
#define OC_PROFILE_MESSAGE(text, size) TracyMessage(text, size)
#define OC_PROFILE_LOCKABLE(type, var, name) TracyLockable(type, var, name)
#define OC_PROFILE_LOCK_MARK(var) LockMark(var)
#define PROFILE_SCOPE() ZoneScoped

#else

#include <chrono>
#include "logging.h"

namespace ocarina {

class Timer {
public:
    explicit Timer(const char* output_text) : output_text_(output_text) {
        start_time_ = std::chrono::steady_clock::now();
    }

    ~Timer() {
        const auto end = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration<double, std::milli>(end - start_time_).count();
        OC_INFO_FORMAT("{} cost: {:.3f} ms", output_text_, ms);
    }

private:
    std::chrono::steady_clock::time_point start_time_;
    const char* output_text_;
};

} // namespace ocarina

#define OC_PROFILE_FUNCTION ((void)0)
#define OC_PROFILE_SCOPE_N(name) ((void)0)
#define OC_PROFILE_SCOPE_C(color) ((void)0)
#define OC_PROFILE_FRAME_MARK ((void)0)
#define OC_PROFILE_FRAME_MARK_N(name) ((void)0)
#define OC_PROFILE_PLOT(name, value) ((void)0)
#define OC_PROFILE_PLOT_CONFIG(name, type) ((void)0)
#define OC_PROFILE_MESSAGE(text, size) ((void)0)
#define OC_PROFILE_LOCKABLE(type, var, name) type var
#define OC_PROFILE_LOCK_MARK(var) ((void)0)
#define PROFILE_SCOPE() ocarina::Timer timer(__FUNCTION__)

#endif

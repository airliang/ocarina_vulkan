#pragma once

#include "core/header.h"
#include "core/stl.h"
#include <atomic>
#include <mutex>

namespace ocarina {

/// Thread-safe loading progress state shared between worker loaders and UI tasks.
class OC_FRAMEWORK_API LoadingProgressListener {
public:
    enum class State : uint8_t {
        Idle,
        Running,
        Completed,
        Failed,
    };

    struct Snapshot {
        State state = State::Idle;
        /// Normalized progress in [0, 1]. Values < 0 mean indeterminate.
        float progress = -1.f;
        uint32_t current_step = 0;
        uint32_t total_steps = 0;
        string title;
        string phase;
        string message;
    };

    void reset() noexcept;

    void begin(const string& title, uint32_t total_steps = 0);
    void set_total_steps(uint32_t total_steps);
    void set_phase(const string& phase);
    void set_message(const string& message);
    void set_step(uint32_t current_step, uint32_t total_steps);
    void advance(uint32_t delta = 1);
    void set_progress(float normalized_progress);
    void complete();
    void fail(const string& message);

    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] float progress() const noexcept;
    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] bool is_indeterminate() const noexcept;

private:
    void update_progress_locked() noexcept;

    mutable std::mutex mutex_;
    Snapshot snapshot_;
    std::atomic<State> state_{State::Idle};
    std::atomic<float> progress_{-1.f};
};

}// namespace ocarina

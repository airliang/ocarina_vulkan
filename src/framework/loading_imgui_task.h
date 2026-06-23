#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/util.h"
#include "rhi/command_buffer.h"
#include "ext/enkiTS/src/TaskScheduler.h"

namespace ocarina {

class Renderer;

class LoadingImguiTask : public enki::IPinnedTask {
public:
    explicit LoadingImguiTask(Renderer& renderer) noexcept;

    void configure(const enki::ICompletable* loader_task) noexcept;

    void Execute() override;

    [[nodiscard]] double last_dt() const noexcept { return dt_; }
    [[nodiscard]] uint64_t execute_thread_id() const noexcept { return execute_thread_id_; }

private:
    void render_loading_frame();

    Renderer& renderer_;
    const enki::ICompletable* loader_task_ = nullptr;
    Clock clock_;
    double dt_ = 0.0;
    uint64_t execute_thread_id_ = 0;
};

}// namespace ocarina

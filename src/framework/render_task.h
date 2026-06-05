#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/util.h"
#include "ext/enkiTS/src/TaskScheduler.h"

namespace ocarina {

class Renderer;

class RenderTask : public enki::IPinnedTask {
public:
    using EndCallback = ocarina::function<void()>;

    explicit RenderTask(Renderer& renderer) noexcept;

    void Execute() override;

    void set_end_callback(EndCallback cb) noexcept { end_callback_ = std::move(cb); }

    [[nodiscard]] double last_dt() const noexcept { return dt_; }

private:
    void render_one_frame();

    void execute_default_render_path();

    Renderer& renderer_;
    EndCallback end_callback_;
    Clock clock_;
    double dt_ = 0.0;
};

}// namespace ocarina

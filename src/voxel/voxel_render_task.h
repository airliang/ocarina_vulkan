#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/util.h"
#include "ext/enkiTS/src/TaskScheduler.h"

namespace ocarina {

class VoxelRenderer;

class VoxelRenderTask : public enki::IPinnedTask {
public:
    using EndCallback = ocarina::function<void()>;

    explicit VoxelRenderTask(VoxelRenderer& renderer) noexcept;

    void Execute() override;

    void set_end_callback(EndCallback cb) noexcept { end_callback_ = std::move(cb); }
    [[nodiscard]] double last_dt() const noexcept { return dt_; }

private:
    void render_one_frame();
    void execute_default_render_path();

    VoxelRenderer& renderer_;
    EndCallback end_callback_;
    Clock clock_;
    double dt_ = 0.0;
};

}// namespace ocarina

//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "TaskScheduler.h"
#include "rhi/command_buffer.h"

namespace ocarina {

class RHIRenderPass;
struct RenderPassTaskContext{
    RHIRenderPass* render_pass_ = nullptr;
    CommandBuffer command_buffer_;
};

class RenderPassTask : public enki::ITaskSet
{
public:

    RenderPassTask(const RenderPassTaskContext& context)
    {
        context_ = context;
    }

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;

private:
    RenderPassTaskContext context_;
};

}// namespace ocarina
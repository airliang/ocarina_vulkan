//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "render_pass_task.h"
#include "enki_task_debug.h"
#include "rhi/renderpass.h"

namespace ocarina {

void RenderPassTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) {
    log_enki_task_execute_range("RenderPassTask", threadnum, range.start, range.end, execute_thread_id_);

    //context_.render_pass_->begin_render_pass(context_.command_buffer_);
    //context_.render_pass_->draw_items(context_.command_buffer_);
    //render_pass->draw_gui();
    if (context_.render_pass_->is_swapchain_renderpass())
    {
        //if (render_gui_impl_)
        //{
        //    render_gui_impl_(render_pass->get_command_buffer());
        //}
    }
    //context_.render_pass_->end_render_pass(context_.command_buffer_);
}
}// namespace ocarina
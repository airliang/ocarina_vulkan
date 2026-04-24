//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "rhi/renderpass.h"
#include "renderer.h"
#include "rhi/device.h"
#include "rhi/command_buffer.h"

namespace ocarina {
Renderer::~Renderer() {
    if (release != nullptr) {
        release();
    }
    render_passes_.clear();
}

void Renderer::set_render_callback(RenderCallback cb) {
    render = cb;
}

void Renderer::set_setup_callback(SetupCallback cb) {
    setup = cb;
    if (setup) {
        setup();
    }
}

void Renderer::set_release_callback(ReleaseCallback cb) {
    release = cb;
    if (release) {
        release();
    }
}

void Renderer::render_frame()
{
    double dt = 0;
    if (update_frame)
        update_frame(dt);
    if (render)
        render(dt);
    else
    {
        device_->begin_frame();
        CommandBuffer cmd = device_->get_command_buffer();
        device_->begin_command_buffer(cmd);
        for (auto& render_pass : render_passes_)
        {
            if (render_pass->is_swapchain_renderpass())
            {
                // Update clear color for swapchain render pass
                cmd.add_signal_semaphore(device_->get_render_complete_semaphore());
                cmd.add_wait_semaphore(device_->get_present_complete_semaphore());
            }
            //render_pass->begin_render_pass(cmd);
            cmd.begin_render_pass(render_pass);
            render_pass->draw_items(cmd);

            if (render_pass->is_swapchain_renderpass())
            {
                if (render_gui_impl_)
                {
                    render_gui_impl_(cmd);
                }
            }
            //render_pass->end_render_pass(cmd);
            cmd.end_render_pass();
        }
        device_->end_command_buffer(cmd);
        device_->execute_command_buffers(&cmd, 1);
        device_->release_command_buffer(cmd);
        device_->end_frame();
        
    }
}

}// namespace ocarina
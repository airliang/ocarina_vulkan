//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "rhi/renderpass.h"
#include "renderer.h"
#include "rhi/device.h"

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
        for (auto& render_pass : render_passes_)
        {
            render_pass->begin_render_pass();
            render_pass->draw_items();
            //render_pass->draw_gui();
            if (render_pass->is_swapchain_renderpass())
            {
                if (render_gui_impl_)
                {
                    render_gui_impl_(render_pass->get_command_buffer());
                }
            }
            render_pass->end_render_pass();
        }
        
        device_->end_frame();
        device_->submit_frame();
    }
}

void Renderer::present_frame()
{
    device_->present_frame();
}

}// namespace ocarina
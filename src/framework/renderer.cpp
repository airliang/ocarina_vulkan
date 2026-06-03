//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "rhi/renderpass.h"
#include "renderer.h"
#include "rhi/device.h"
#include "rhi/command_buffer.h"
#include "rhi/pipeline_state.h"
#include "resource_manager.h"
#include "frame_resources.h"
#include "TaskScheduler.h" // enki task scheduler for AddPinnedTask and WaitforTask

namespace ocarina {
Renderer::~Renderer() {
    if (release != nullptr) {
        release();
    }
    render_passes_.clear();
    ResourceManager::instance().cleanup();
    task_scheduler_.WaitforAllAndShutdown();
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

void Renderer::render_frame(double dt)
{
    // execute loading: if an async task set is set, schedule it on a worker thread and wait for it
    if (async_loader_task_) {

        task_scheduler_.AddTaskSetToPipe(async_loader_task_);

        if (async_wait_fn_) {
            // application-provided wait function should wait only for this loader task
            async_wait_fn_();
        }

        task_scheduler_.WaitforTask(async_loader_task_);

        if (async_complete_fn_) {
            async_complete_fn_();
        }

        // Clear loader after run (single-shot). If you want persistent loaders, change this behavior.
        async_loader_task_ = nullptr;
        async_wait_fn_ = nullptr;
        async_complete_fn_ = nullptr;
    }

    //wait loading finish and execute render task

    if (update_frame)
        update_frame(dt);
    FrameResources::instance().update_per_frame(dt);
    if (render)
        render(dt);
    else
    {
        
        device_->begin_frame();
        CommandBuffer cmd = device_->get_command_buffer();
        cmd.begin();
        std::vector<DescriptorSet*> global_descriptor_sets = FrameResources::instance().global_descriptor_sets_array();
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
            const auto& queues = render_pass->pipeline_render_queues();
            if (!queues.empty()) {
                RHIPipeline* pipeline = queues.begin()->first;

                FrameResources& frame_resources = FrameResources::instance();

                if (frame_resources.has_global_ubo_descriptor_set()) {
                    DescriptorSet* global_ubo_set = frame_resources.get_global_ubo_descriptor_set();
                    cmd.bind_descriptor_sets(
                        &global_ubo_set,
                        frame_resources.global_ubo_descriptor_set_index(),
                        1,
                        pipeline->pipeline_layout);
                } else if (!global_descriptor_sets.empty()) {
                    cmd.bind_descriptor_sets(
                        global_descriptor_sets.data(),
                        (uint32_t)DescriptorSetIndex::GLOBAL_SET,
                        static_cast<uint32_t>(global_descriptor_sets.size()),
                        pipeline->pipeline_layout);
                }

                if (frame_resources.has_bindless_descriptor_set()) {
                    DescriptorSet* bindless_set = frame_resources.get_bindless_descriptor_set();
                    cmd.bind_descriptor_sets(
                        &bindless_set,
                        frame_resources.bindless_descriptor_set_index(),
                        1,
                        pipeline->pipeline_layout);
                }
            }
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
        cmd.end();
        device_->execute_command_buffers(&cmd, 1);
        device_->release_command_buffer(cmd);
        device_->end_frame();
        
    }
}

}// namespace ocarina
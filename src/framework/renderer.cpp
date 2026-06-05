//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "renderer.h"
#include "render_task.h"
#include "resource_manager.h"
#include "rhi/device.h"

namespace ocarina {

Renderer::Renderer(Device *device)
    : device_(device),
      render_task_(*this)
{
    task_scheduler_.Initialize();
}

Renderer::~Renderer() {
    shutdown();
    render_passes_.clear();
    ResourceManager::instance().cleanup();
}

void Renderer::shutdown() {
    if (shutdown_called_) {
        return;
    }
    shutdown_called_ = true;
    task_scheduler_.WaitforAllAndShutdown();
}

void Renderer::set_render_callback(RenderCallback cb) {
    render = cb;
}

void Renderer::run()
{
    if (async_loader_task_) {
        task_scheduler_.AddTaskSetToPipe(async_loader_task_);

        if (async_wait_fn_) {
            async_wait_fn_();
        }

        task_scheduler_.WaitforTask(async_loader_task_);

        if (async_complete_fn_) {
            async_complete_fn_();
        }

        async_loader_task_ = nullptr;
        async_wait_fn_ = nullptr;
        async_complete_fn_ = nullptr;
    }

    task_scheduler_.AddPinnedTask(&render_task_);
}

}// namespace ocarina

#include "async_loader.h"
#include "enki_task_debug.h"
#include "loading_progress_listener.h"
#include "rhi/device.h"

namespace ocarina {

void AsyncLoader::run_shader_compile_task() noexcept {
    if (scheduler_ == nullptr || shader_entries_ == nullptr || shader_entries_->empty() || device_ == nullptr) {
        return;
    }

    const uint32_t shader_count = static_cast<uint32_t>(shader_entries_->size());
    const uint32_t load_steps = count_load_progress_steps();
    const uint32_t total_steps = shader_count + load_steps;
    if (progress_listener_ != nullptr && total_steps > 0) {
        progress_listener_->begin("Loading", total_steps);
        progress_listener_->set_phase("Compiling shaders");
    }

    ShaderCompileTask compile_task;
    compile_task.configure(device_, shader_entries_, progress_listener_);
    scheduler_->AddTaskSetToPipe(&compile_task);
    scheduler_->WaitforTask(&compile_task);
}

void AsyncLoader::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) {
    (void)range;
    (void)threadnum;

    run_shader_compile_task();

    if (task_) {
        try {
            task_(device_);
        } catch (...) {
        }
    } else {
        load(device_);
    }
}

}// namespace ocarina

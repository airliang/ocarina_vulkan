#include "shader_compile_task.h"
#include "loading_progress_listener.h"
#include "rhi/device.h"

namespace ocarina {

void ShaderCompileTask::configure(
    Device* device,
    std::vector<Entry>* entries,
    LoadingProgressListener* progress_listener) noexcept
{
    device_ = device;
    entries_ = entries;
    progress_listener_ = progress_listener;
    const uint32_t entry_count = entries_ != nullptr ? static_cast<uint32_t>(entries_->size()) : 0u;
    m_SetSize = entry_count;
    m_MinRange = 1;
}

void ShaderCompileTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) {
    (void)threadnum;
    if (device_ == nullptr || entries_ == nullptr) {
        return;
    }

    for (uint32_t index = range.start; index < range.end; ++index) {
        Entry& entry = (*entries_)[index];
        if (entry.file_path.empty()) {
            continue;
        }
        entry.shader = device_->create_shader_from_file(entry.file_path, entry.shader_type, entry.options);
        if (progress_listener_ != nullptr) {
            progress_listener_->advance();
        }
    }
}

}// namespace ocarina

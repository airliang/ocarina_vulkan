#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "rhi/graphics_descriptions.h"
#include "ext/enkiTS/src/TaskScheduler.h"

namespace ocarina {

class Device;
class LoadingProgressListener;

class ShaderCompileTask : public enki::ITaskSet {
public:
    struct Entry {
        std::string file_path;
        ShaderType shader_type = ShaderType::VertexShader;
        std::set<string> options;
        handle_ty shader = 0;
    };

    void configure(
        Device* device,
        std::vector<Entry>* entries,
        LoadingProgressListener* progress_listener = nullptr) noexcept;

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;

private:
    Device* device_ = nullptr;
    std::vector<Entry>* entries_ = nullptr;
    LoadingProgressListener* progress_listener_ = nullptr;
};

}// namespace ocarina

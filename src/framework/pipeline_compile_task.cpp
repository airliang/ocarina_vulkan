#include "pipeline_compile_task.h"

#include "loading_progress_listener.h"
#include "pipeline_manager.h"
#include "rhi/device.h"
#include "rhi/pipeline_state.h"

#include <utility>

namespace ocarina {

namespace {

void resolve_shader_source(
    Device* device,
    PipelineCompileTask::ShaderSource& source,
    ShaderType shader_type,
    LoadingProgressListener* progress_listener) {
    if (device == nullptr) {
        return;
    }

    if (!PipelineCompileTask::Entry::shader_needs_compile(source.shader)) {
        return;
    }

    if (source.file_path.empty()) {
        return;
    }

    // create_shader_from_file hits the driver shader cache when the key matches.
    source.shader = device->create_shader_from_file(source.file_path, shader_type, source.options);
    if (progress_listener != nullptr) {
        progress_listener->advance();
    }
}

}// namespace

uint32_t PipelineCompileTask::Entry::pending_shader_count() const noexcept {
    if (is_graphics()) {
        uint32_t count = 0;
        if (shader_needs_compile(graphics.vertex.shader)) {
            ++count;
        }
        if (shader_needs_compile(graphics.pixel.shader)) {
            ++count;
        }
        return count;
    }
    return shader_needs_compile(compute.shader) ? 1u : 0u;
}

bool PipelineCompileTask::Entry::shaders_resolved() const noexcept {
    if (is_graphics()) {
        return !shader_needs_compile(graphics.vertex.shader)
            && !shader_needs_compile(graphics.pixel.shader);
    }
    return !shader_needs_compile(compute.shader);
}

PipelineCompileTask::Entry::Entry() noexcept
    : kind(Kind::Graphics) {
    new (&graphics) GraphicsProgram();
}

PipelineCompileTask::Entry::~Entry() {
    destroy();
}

PipelineCompileTask::Entry::Entry(const Entry& other)
    : kind(other.kind) {
    copy_from(other);
}

PipelineCompileTask::Entry::Entry(Entry&& other) noexcept
    : kind(other.kind) {
    move_from(std::move(other));
}

PipelineCompileTask::Entry& PipelineCompileTask::Entry::operator=(const Entry& other) {
    if (this == &other) {
        return *this;
    }
    destroy();
    kind = other.kind;
    copy_from(other);
    return *this;
}

PipelineCompileTask::Entry& PipelineCompileTask::Entry::operator=(Entry&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    destroy();
    kind = other.kind;
    move_from(std::move(other));
    return *this;
}

void PipelineCompileTask::Entry::destroy() noexcept {
    if (kind == Kind::Graphics) {
        graphics.~GraphicsProgram();
    } else {
        compute.~ShaderSource();
    }
}

void PipelineCompileTask::Entry::copy_from(const Entry& other) {
    if (other.kind == Kind::Graphics) {
        new (&graphics) GraphicsProgram(other.graphics);
    } else {
        new (&compute) ShaderSource(other.compute);
    }
}

void PipelineCompileTask::Entry::move_from(Entry&& other) noexcept {
    if (other.kind == Kind::Graphics) {
        new (&graphics) GraphicsProgram(std::move(other.graphics));
    } else {
        new (&compute) ShaderSource(std::move(other.compute));
    }
}

PipelineCompileTask::Entry PipelineCompileTask::Entry::make_graphics(
    std::string vertex_path,
    std::string pixel_path,
    std::set<string> vertex_options,
    std::set<string> pixel_options) {
    Entry entry;
    entry.kind = Kind::Graphics;
    entry.graphics.vertex.file_path = std::move(vertex_path);
    entry.graphics.vertex.options = std::move(vertex_options);
    entry.graphics.pixel.file_path = std::move(pixel_path);
    entry.graphics.pixel.options = std::move(pixel_options);
    return entry;
}

PipelineCompileTask::Entry PipelineCompileTask::Entry::make_compute(
    std::string compute_path,
    std::set<string> options) {
    Entry entry;
    entry.destroy();
    entry.kind = Kind::Compute;
    new (&entry.compute) ShaderSource();
    entry.compute.file_path = std::move(compute_path);
    entry.compute.options = std::move(options);
    return entry;
}

void PipelineCompileTask::reset() noexcept {
    device_ = nullptr;
    manager_ = nullptr;
    pool_ = nullptr;
    progress_listener_ = nullptr;
    entry_ = nullptr;
    pipeline_state_ = {};
    render_pass_ = nullptr;
    compile_from_entry_ = false;
    threadNum = 1;
}

void PipelineCompileTask::Initialize(
    Device* device,
    PipelineManager* manager,
    PipelineCompileTaskPool* pool,
    Entry* entry,
    RHIRenderPass* render_pass,
    uint32_t worker_thread_num,
    LoadingProgressListener* progress_listener) noexcept {
    reset();
    device_ = device;
    manager_ = manager;
    pool_ = pool;
    entry_ = entry;
    render_pass_ = render_pass;
    progress_listener_ = progress_listener;
    compile_from_entry_ = true;
    pipeline_state_ = MakePipelineStateFromEntry(entry);
    threadNum = worker_thread_num == 0 ? 1 : worker_thread_num;
}

void PipelineCompileTask::Initialize(
    Device* device,
    PipelineManager* manager,
    PipelineCompileTaskPool* pool,
    const PipelineState& pipeline_state,
    RHIRenderPass* render_pass,
    uint32_t worker_thread_num) noexcept {
    reset();
    device_ = device;
    manager_ = manager;
    pool_ = pool;
    pipeline_state_ = pipeline_state;
    render_pass_ = render_pass;
    compile_from_entry_ = false;
    threadNum = worker_thread_num == 0 ? 1 : worker_thread_num;
}

PipelineState PipelineCompileTask::MakePipelineStateFromEntry(const Entry* entry) noexcept {
    if (entry != nullptr && entry->is_graphics()) {
        return PipelineState::MakeGraphicsDefault(entry->vertex_shader(), entry->pixel_shader());
    }
    return PipelineState::MakeGraphicsDefault(InvalidUI64, InvalidUI64);
}

void PipelineCompileTask::ResolveEntryShaders(
    Device* device,
    Entry* entry,
    LoadingProgressListener* progress_listener) noexcept {
    if (device == nullptr || entry == nullptr) {
        return;
    }

    if (entry->is_graphics()) {
        resolve_shader_source(
            device,
            entry->graphics.vertex,
            ShaderType::VertexShader,
            progress_listener);
        resolve_shader_source(
            device,
            entry->graphics.pixel,
            ShaderType::PixelShader,
            progress_listener);
        return;
    }

    resolve_shader_source(
        device,
        entry->compute,
        ShaderType::ComputeShader,
        progress_listener);
}

void PipelineCompileTask::resolve_shaders_from_entry() noexcept {
    if (device_ == nullptr || entry_ == nullptr) {
        return;
    }

    ResolveEntryShaders(device_, entry_, progress_listener_);
    if (entry_->is_graphics()) {
        pipeline_state_ = MakePipelineStateFromEntry(entry_);
    }
}

void PipelineCompileTask::create_layouts_and_pipeline() noexcept {
    if (manager_ == nullptr || device_ == nullptr || render_pass_ == nullptr) {
        return;
    }

    if (pipeline_state_.shaders[0] == 0 || pipeline_state_.shaders[0] == InvalidUI64
        || pipeline_state_.shaders[1] == 0 || pipeline_state_.shaders[1] == InvalidUI64) {
        return;
    }

    if (manager_->has_pipeline(pipeline_state_, render_pass_)) {
        return;
    }

    RHIPipelineLayout* pipeline_layout =
        manager_->create_and_cache_pipeline_layout(pipeline_state_.shaders);
    if (pipeline_layout == nullptr) {
        return;
    }

    RHIPipeline* pipeline = device_->create_pipeline(
        pipeline_state_,
        render_pass_,
        pipeline_layout);
    if (pipeline != nullptr) {
        manager_->insert_pipeline_cache(pipeline_state_, render_pass_, pipeline);
    }
}

void PipelineCompileTask::finish_and_release() noexcept {
    if (manager_ != nullptr && render_pass_ != nullptr) {
        manager_->on_compile_task_finished(pipeline_state_, render_pass_);
    }
    if (pool_ != nullptr) {
        pool_->Release(this);
    }
}

void PipelineCompileTask::Execute() {
    if (compile_from_entry_) {
        resolve_shaders_from_entry();
    }

    if (entry_ != nullptr && entry_->is_compute()) {
        finish_and_release();
        return;
    }

    create_layouts_and_pipeline();
    finish_and_release();
}

PipelineCompileTaskPool::~PipelineCompileTaskPool() {
    clear();
}

PipelineCompileTask* PipelineCompileTaskPool::Acquire() {
    reclaim();

    std::lock_guard<std::mutex> lock(mutex_);
    while (!free_.empty()) {
        PipelineCompileTask* task = free_.front();
        free_.pop_front();
        if (task != nullptr && task->GetIsComplete()) {
            return task;
        }
        if (task != nullptr) {
            pending_release_.push_back(task);
        }
    }

    auto* task = ocarina::new_with_allocator<PipelineCompileTask>();
    owned_.push_back(task);
    return task;
}

void PipelineCompileTaskPool::Release(PipelineCompileTask* task) {
    if (task == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    pending_release_.push_back(task);
}

void PipelineCompileTaskPool::reclaim() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::deque<PipelineCompileTask*> still_running;
    while (!pending_release_.empty()) {
        PipelineCompileTask* task = pending_release_.front();
        pending_release_.pop_front();
        if (task == nullptr) {
            continue;
        }
        if (!task->GetIsComplete()) {
            still_running.push_back(task);
            continue;
        }
        free_.push_back(task);
    }
    for (PipelineCompileTask* task : still_running) {
        pending_release_.push_back(task);
    }
}

void PipelineCompileTaskPool::clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    free_.clear();
    pending_release_.clear();
    for (PipelineCompileTask* task : owned_) {
        ocarina::delete_with_allocator(task);
    }
    owned_.clear();
}

}// namespace ocarina

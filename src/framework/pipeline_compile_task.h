#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/pipeline_state.h"
#include "ext/enkiTS/src/TaskScheduler.h"

#include <deque>
#include <mutex>
#include <vector>

namespace ocarina {

class Device;
class LoadingProgressListener;
class PipelineManager;
class PipelineCompileTaskPool;
class RHIRenderPass;

// Compiles shaders (or hits the shader cache), builds descriptor-set / pipeline layouts,
// creates the graphics PSO, and inserts it into PipelineManager's cache.
// Each task handles exactly one PipelineCompileTarget or one runtime PipelineState + pass.
class PipelineCompileTask : public enki::IPinnedTask {
public:
    struct ShaderSource {
        std::string file_path;
        std::set<string> options;
        handle_ty shader = 0;
    };

    struct GraphicsProgram {
        ShaderSource vertex;
        ShaderSource pixel;
    };

    // One Entry is either a VS+PS pair or a single compute shader.
    struct Entry {
        enum class Kind : uint8_t {
            Graphics,
            Compute,
        };

        Kind kind = Kind::Graphics;
        union {
            GraphicsProgram graphics;
            ShaderSource compute;
        };

        Entry() noexcept;
        Entry(const Entry& other);
        Entry(Entry&& other) noexcept;
        Entry& operator=(const Entry& other);
        Entry& operator=(Entry&& other) noexcept;
        ~Entry();

        [[nodiscard]] static Entry make_graphics(
            std::string vertex_path,
            std::string pixel_path,
            std::set<string> vertex_options = {},
            std::set<string> pixel_options = {});

        [[nodiscard]] static Entry make_compute(
            std::string compute_path,
            std::set<string> options = {});

        [[nodiscard]] bool is_graphics() const noexcept { return kind == Kind::Graphics; }
        [[nodiscard]] bool is_compute() const noexcept { return kind == Kind::Compute; }

        [[nodiscard]] static bool shader_needs_compile(handle_ty shader) noexcept {
            return shader == 0 || shader == InvalidUI64;
        }

        [[nodiscard]] uint32_t shader_count() const noexcept {
            return is_graphics() ? 2u : 1u;
        }

        [[nodiscard]] uint32_t pending_shader_count() const noexcept;

        [[nodiscard]] bool shaders_resolved() const noexcept;

        [[nodiscard]] handle_ty vertex_shader() const noexcept {
            return is_graphics() ? graphics.vertex.shader : 0;
        }
        [[nodiscard]] handle_ty pixel_shader() const noexcept {
            return is_graphics() ? graphics.pixel.shader : 0;
        }
        [[nodiscard]] handle_ty compute_shader() const noexcept {
            return is_compute() ? compute.shader : 0;
        }

    private:
        void destroy() noexcept;
        void copy_from(const Entry& other);
        void move_from(Entry&& other) noexcept;
    };

    PipelineCompileTask() = default;

    // Async load: one Entry + one render pass.
    void Initialize(
        Device* device,
        PipelineManager* manager,
        PipelineCompileTaskPool* pool,
        PipelineCompileTask::Entry* entry,
        RHIRenderPass* render_pass,
        uint32_t worker_thread_num,
        LoadingProgressListener* progress_listener = nullptr) noexcept;

    // Runtime: shaders already resolved in pipeline_state.
    void Initialize(
        Device* device,
        PipelineManager* manager,
        PipelineCompileTaskPool* pool,
        const PipelineState& pipeline_state,
        RHIRenderPass* render_pass,
        uint32_t worker_thread_num) noexcept;

    void Execute() override;

    [[nodiscard]] static PipelineState MakePipelineStateFromEntry(const Entry* entry) noexcept;
    static void ResolveEntryShaders(
        Device* device,
        Entry* entry,
        LoadingProgressListener* progress_listener = nullptr) noexcept;

    [[nodiscard]] const PipelineState& pipeline_state() const noexcept { return pipeline_state_; }
    [[nodiscard]] RHIRenderPass* render_pass() const noexcept { return render_pass_; }

private:
    void reset() noexcept;
    void resolve_shaders_from_entry() noexcept;
    void create_layouts_and_pipeline() noexcept;
    void finish_and_release() noexcept;

    Device* device_ = nullptr;
    PipelineManager* manager_ = nullptr;
    PipelineCompileTaskPool* pool_ = nullptr;
    LoadingProgressListener* progress_listener_ = nullptr;
    Entry* entry_ = nullptr;
    PipelineState pipeline_state_{};
    RHIRenderPass* render_pass_ = nullptr;
    bool compile_from_entry_ = false;
};

// One async compile unit: a single Entry compiled against one render pass.
struct PipelineCompileTarget {
    PipelineCompileTask::Entry* entry = nullptr;
    RHIRenderPass* render_pass = nullptr;
};

// Pool of reusable PipelineCompileTask instances.
// Release() from Execute() is deferred until GetIsComplete() so enkiTS stays valid.
class PipelineCompileTaskPool {
public:
    PipelineCompileTaskPool() = default;
    ~PipelineCompileTaskPool();

    PipelineCompileTaskPool(const PipelineCompileTaskPool&) = delete;
    PipelineCompileTaskPool& operator=(const PipelineCompileTaskPool&) = delete;

    [[nodiscard]] PipelineCompileTask* Acquire();
    void Release(PipelineCompileTask* task);
    void reclaim() noexcept;
    void clear() noexcept;

private:
    std::mutex mutex_;
    std::vector<PipelineCompileTask*> owned_;
    std::deque<PipelineCompileTask*> free_;
    std::deque<PipelineCompileTask*> pending_release_;
};

}// namespace ocarina

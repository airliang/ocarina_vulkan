#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/concepts.h"
#include "core/thread_safe_queue.h"
#include "mesh_cpu_memory.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/resources/texture_sampler.h"
#include "rhi/vertex_buffer.h"
#include "ext/enkiTS/src/TaskScheduler.h"
#include <atomic>
#include <memory>

namespace ocarina {

class Device;
class Texture;
class Cubemap;
class Mesh;
class StagingUploader;

enum class GPUResourceRequestType : uint8_t {
    TextureFromData,
    RenderTarget,
    Mesh,
    CubemapFromData,
};

/// Base GPU create/upload request (moved onto the GPU resource thread).
struct GPUResourceRequest {
    GPUResourceRequest() = default;
    GPUResourceRequest(const GPUResourceRequest&) = delete;
    GPUResourceRequest& operator=(const GPUResourceRequest&) = delete;
    GPUResourceRequest(GPUResourceRequest&&) noexcept = default;
    GPUResourceRequest& operator=(GPUResourceRequest&&) noexcept = default;
    virtual ~GPUResourceRequest() = default;

    [[nodiscard]] virtual GPUResourceRequestType type() const noexcept = 0;
    virtual void process() = 0;

    Device* device = nullptr;
    std::string name;
};

/// Upload CPU pixels or finalize bindless binding for an existing Texture.
struct TextureGPUResourceRequest : GPUResourceRequest {
    explicit TextureGPUResourceRequest(Device* device, Texture* texture);

    GPUResourceRequestType kind = GPUResourceRequestType::TextureFromData;
    /// Owned pixel bytes for TextureFromData (moved into the GPU thread).
    ocarina_vector<uint8_t> pixel_data{
        ocarina_pool_allocator<uint8_t>("TextureGPUResourceRequest.pixel_data")};
    /// Pre-allocated bindless slot (InvalidUI32 for non-bindless render targets).
    uint32_t bindless_index = InvalidUI32;
    Texture* texture_ = nullptr;

    [[nodiscard]] GPUResourceRequestType type() const noexcept override { return kind; }
    void process() override;
};

/// Mesh geometry upload into MeshBufferAllocator pages (GPU copy on this thread).
/// Geometry is written onto @p mesh; Mesh becomes GPU_Ready after upload.
struct MeshGPUResourceRequest : GPUResourceRequest {
    MeshPositions positions = make_mesh_positions();
    MeshNormals normals = make_mesh_normals();
    MeshTangents tangents = make_mesh_tangents();
    MeshUvs uvs = make_mesh_uvs();
    MeshColors colors = make_mesh_colors();
    MeshIndices indices = make_mesh_indices();

    Mesh* mesh = nullptr;

    [[nodiscard]] GPUResourceRequestType type() const noexcept override {
        return GPUResourceRequestType::Mesh;
    }
    void process() override;
};

/// Upload six cube faces for an existing Cubemap allocation.
struct CubemapGPUResourceRequest : GPUResourceRequest {
    explicit CubemapGPUResourceRequest(Device* device, Cubemap* cubemap);

    ocarina_vector<uint8_t> pixel_data{
        ocarina_pool_allocator<uint8_t>("CubemapGPUResourceRequest.pixel_data")};
    Cubemap* cubemap_ = nullptr;

    [[nodiscard]] GPUResourceRequestType type() const noexcept override {
        return GPUResourceRequestType::CubemapFromData;
    }
    void process() override;
};

/// Pinned thread that serializes Vulkan texture/mesh uploads.
class OC_FRAMEWORK_API GPUResourceThread : public enki::IPinnedTask, public concepts::Noncopyable {
public:
    static GPUResourceThread& instance();

    GPUResourceThread() noexcept;

    void Execute() override;

    /// Start the pinned loop on the task scheduler (call once after Initialize).
    void start(enki::TaskScheduler& scheduler, Device* device);

    /// Wake the loop so it can exit on scheduler shutdown.
    void request_shutdown();

    /// Destroy device-side state after the pinned thread has stopped.
    void shutdown();

    [[nodiscard]] bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    /// Enqueue a request for async processing on the GPU resource thread.
    /// If the thread is not running yet, processes immediately on the caller.
    void enqueue(std::shared_ptr<GPUResourceRequest> request);

    /// Shared growable staging uploader for this thread's CPU → GPU copies.
    [[nodiscard]] StagingUploader &staging_uploader();

private:
    [[nodiscard]] bool should_stop() const noexcept;
    void ensure_staging_uploader();

    ThreadSafeQueue<std::shared_ptr<GPUResourceRequest>> queue_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_requested_{false};
    enki::TaskScheduler* scheduler_ = nullptr;
    Device* device_ = nullptr;
    StagingUploader* staging_uploader_ = nullptr;
};

}// namespace ocarina

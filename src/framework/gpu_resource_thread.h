#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/concepts.h"
#include "core/thread_safe_queue.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/resources/texture_sampler.h"
#include "rhi/vertex_buffer.h"
#include "ext/enkiTS/src/TaskScheduler.h"
#include <atomic>
#include <memory>
#include <vector>

namespace ocarina {

class Device;
class Texture;
class Mesh;

enum class GPUResourceRequestType : uint8_t {
    TextureFromData,
    RenderTarget,
    Mesh,
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

/// Texture (from CPU pixels) or render-target create request.
struct TextureGPUResourceRequest : GPUResourceRequest {
    GPUResourceRequestType kind = GPUResourceRequestType::TextureFromData;
    TextureViewCreation texture_view{};
    TextureSampler sampler{};
    PixelStorage pixel_storage = PixelStorage::BYTE4;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    TextureUsageFlags usage = TextureUsageFlags::None;
    /// Owned pixel bytes for TextureFromData (moved into the GPU thread).
    std::vector<uint8_t> pixel_data;

    /// Pre-allocated bindless slot (InvalidUI32 for non-bindless render targets).
    uint32_t bindless_index = InvalidUI32;
    /// ResourceManager cache key used to publish Texture* when creation finishes.
    uint64_t cache_key = 0;
    bool has_cache_key = false;

    /// Optional immediate out pointer (render-target sync path).
    Texture** out_texture = nullptr;

    [[nodiscard]] GPUResourceRequestType type() const noexcept override { return kind; }
    void process() override;
};

/// Mesh geometry upload into MeshBufferAllocator pages (GPU copy on this thread).
/// Geometry is written onto @p mesh; Mesh becomes GPU_Ready after upload.
struct MeshGPUResourceRequest : GPUResourceRequest {
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> uvs;
    std::vector<Vector4> colors;
    std::vector<uint16_t> indices;

    Mesh* mesh = nullptr;

    [[nodiscard]] GPUResourceRequestType type() const noexcept override {
        return GPUResourceRequestType::Mesh;
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
    void start(enki::TaskScheduler& scheduler);

    /// Wake the loop so it can exit on scheduler shutdown.
    void request_shutdown();

    [[nodiscard]] bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    /// Enqueue a request for async processing on the GPU resource thread.
    /// If the thread is not running yet, processes immediately on the caller.
    void enqueue(std::shared_ptr<GPUResourceRequest> request);

private:
    [[nodiscard]] bool should_stop() const noexcept;

    ThreadSafeQueue<std::shared_ptr<GPUResourceRequest>> queue_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_requested_{false};
    enki::TaskScheduler* scheduler_ = nullptr;
};

}// namespace ocarina

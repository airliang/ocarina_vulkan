#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/concepts.h"
#include "core/thread_safe_queue.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/pipeline_state.h"
#include "rhi/resources/buffer.h"
#include "rhi/resources/texture.h"
#include "rhi/resources/texture_sampler.h"
#include "global_uniform_buffer.h"
#include "entity_component_system.h"
#include "bindless_texture_registry.h"
#include <mutex>

namespace ocarina {
class DescriptorSetLayout;
class DescriptorSet;
class Camera;
class Device;
class Material;

enum class MaterialUpdateKind : uint8_t {
    Texture = 0,
    Sampler,
    UniformBuffer,
};

/// Queued material descriptor / parameter update (processed on the render thread).
struct MaterialUpdateRequest {
    Material* material = nullptr;
    MaterialUpdateKind kind = MaterialUpdateKind::Texture;
    uint64_t name_id = 0;
    TextureHandle texture_handle{};
    TextureSampler sampler{};

    static MaterialUpdateRequest make_texture(
        Material* material,
        uint64_t name_id,
        const TextureHandle& texture_handle) {
        MaterialUpdateRequest request;
        request.material = material;
        request.kind = MaterialUpdateKind::Texture;
        request.name_id = name_id;
        request.texture_handle = texture_handle;
        return request;
    }

    static MaterialUpdateRequest make_sampler(
        Material* material,
        uint64_t name_id,
        const TextureSampler& sampler) {
        MaterialUpdateRequest request;
        request.material = material;
        request.kind = MaterialUpdateKind::Sampler;
        request.name_id = name_id;
        request.sampler = sampler;
        return request;
    }

    static MaterialUpdateRequest make_uniform_buffer(Material* material) {
        MaterialUpdateRequest request;
        request.material = material;
        request.kind = MaterialUpdateKind::UniformBuffer;
        return request;
    }
};

class OC_FRAMEWORK_API FrameResources : public concepts::Noncopyable {
public:
    using UpdateCallback = ocarina::function<void(FrameResources&, double)>;

    static FrameResources& instance();

    void initialize(Device* device);

    void add_global_descriptor_set(uint64_t name_id, DescriptorSet* descriptor_set);

    void add_global_descriptor_set(const std::string& name, DescriptorSet* descriptor_set) {
        add_global_descriptor_set(hash64(name), descriptor_set);
    }

    DescriptorSet* get_global_descriptor_set(uint64_t name_id) const;
    DescriptorSet* get_global_descriptor_set(const std::string& name) const;

    /// Create-or-return a process-wide descriptor set at @p descriptor_set_index.
    /// Global sets are stored in a vector indexed by set index (starting at 0).
    DescriptorSet* get_or_create_descriptor_set(
        uint32_t descriptor_set_index,
        const std::vector<uint64_t>& binding_name_ids,
        ocarina::function<DescriptorSet* ()> create_descriptor_set);

    DescriptorSet* get_descriptor_set(uint32_t descriptor_set_index) const;

    const std::vector<DescriptorSet*>& global_descriptor_sets() const {
        return global_descriptor_sets_;
    }

    std::vector<DescriptorSet*>& global_descriptor_sets() {
        return global_descriptor_sets_;
    }

    bool has_descriptor_set(uint32_t descriptor_set_index) const;

    /// True when this layout requests a process-wide singleton (by binding name / bindless).
    /// Known roles: `global_ubo` (FRAME), `transforms` (SCENE),
    /// `g_textures` / `samplers` / `g_materials` / bindless (MATERIAL).
    [[nodiscard]] static bool is_global_singleton_layout(const DescriptorSetLayout* layout) noexcept;

    /// Create any FRAME/SCENE/shared-bindless sets declared by @p pipeline_layout (idempotent).
    void ensure_global_descriptor_sets(const RHIPipelineLayout* pipeline_layout);

    /// Queue a bindless descriptor write (safe from loader / GPU-resource threads).
    /// Flushed on the render thread in update_per_frame().
    void queue_bindless_texture_update(uint32_t index, Texture* texture);

    /// Legacy name: queues the update (does not write descriptors immediately).
    void update_bindless_texture_at_index(uint32_t index, Texture* texture) {
        queue_bindless_texture_update(index, texture);
    }

    /// Queue a material texture / sampler / uniform-buffer update (safe from any thread).
    void queue_material_update(MaterialUpdateRequest request);

    bool is_global_descriptor_set_index(uint32_t set_index) const;

    [[nodiscard]] GlobalUniformBuffer& global_uniform_buffer() noexcept { return global_ubo_; }
    [[nodiscard]] const GlobalUniformBuffer& global_uniform_buffer() const noexcept { return global_ubo_; }

    /// GPU buffer for `global_ubo` (FRAME_SET).
    [[nodiscard]] TypedBuffer<GlobalUniformBuffer>& global_ubo_buffer() noexcept { return global_ubo_buffer_; }
    [[nodiscard]] const TypedBuffer<GlobalUniformBuffer>& global_ubo_buffer() const noexcept { return global_ubo_buffer_; }

    /// StructuredBuffer<Transform> transforms SSBO (SCENE_SET / BIND_TRANSFORM).
    [[nodiscard]] TypedBuffer<GPUTransform>& transform_buffer() noexcept { return transform_buffer_; }
    [[nodiscard]] const TypedBuffer<GPUTransform>& transform_buffer() const noexcept { return transform_buffer_; }

    /// StructuredBuffer<MaterialParams> g_materials (MATERIAL_SET / BIND_MATERIAL).
    [[nodiscard]] TypedBuffer<MaterialParams>& material_buffer() noexcept { return material_buffer_; }
    [[nodiscard]] const TypedBuffer<MaterialParams>& material_buffer() const noexcept { return material_buffer_; }

    void set_sun_direction(const float3& direction) noexcept;
    void set_sun_color(const float3& color) noexcept;
    void set_sun_intensity(float intensity) noexcept;
    void set_light_position(const float3& position) noexcept;

    /// Optional extension hook (global UBO is always updated by the framework first).
    void set_update_callback(UpdateCallback cb) {
        update_ = std::move(cb);
    }

    /// Called on the render thread each frame. Uploads globals, flushes bindless / material queues.
    void update_per_frame(double dt, Camera* camera = nullptr);

    /// Destroy owned GPU buffers. Must be called while Device / VkDevice is still alive
    /// (e.g. from Renderer::shutdown). Safe to call multiple times.
    void release_gpu_buffers();

private:
    FrameResources() = default;
    ~FrameResources() { release_gpu_buffers(); }

    void upload_global_uniform_buffer(Camera* camera);
    void upload_transform_buffer();
    void flush_pending_bindless_updates();
    void process_material_update();
    void create_default_gpu_buffers();
    void grow_transform_gpu_buffer(size_t element_count);
    void grow_material_gpu_buffer(size_t byte_count);
    void bind_global_ubo_if_needed();
    void bind_transform_storage_buffer_if_needed();
    void bind_material_storage_buffer_if_needed();
    DescriptorSet* find_bindless_descriptor_set_locked() const;

    Device* device_ = nullptr;

    mutable std::mutex global_descriptor_sets_mutex_;
    /// Indexed by Vulkan descriptor set index; contiguous from 0 (nullptr = unused hole).
    std::vector<DescriptorSet*> global_descriptor_sets_;
    /// Binding-name aliases for uploads (e.g. "global_ubo", "g_textures", "transforms").
    std::unordered_map<uint64_t, DescriptorSet*> global_descriptor_sets_by_name_;

    struct PendingBindlessUpdate {
        uint32_t index = InvalidUI32;
        Texture* texture = nullptr;
    };
    ThreadSafeQueue<PendingBindlessUpdate> pending_bindless_updates_;

    ThreadSafeQueue<MaterialUpdateRequest> material_update_queue_;

    GlobalUniformBuffer global_ubo_{};
    TypedBuffer<GlobalUniformBuffer> global_ubo_buffer_{};
    bool global_ubo_descriptor_bound_ = false;
    TypedBuffer<GPUTransform> transform_buffer_{};
    bool transform_storage_descriptor_bound_ = false;
    TypedBuffer<MaterialParams> material_buffer_{};
    bool material_storage_descriptor_bound_ = false;
    UpdateCallback update_ = nullptr;
};

}// namespace ocarina

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/concepts.h"
#include "core/thread_safe_list.h"
#include "core/thread_safe_queue.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/pipeline_state.h"
#include "rhi/resources/buffer.h"
#include "rhi/resources/texture.h"
#include "rhi/resources/texture_sampler.h"
#include "global_uniform_buffer.h"
#include "entity_component_system.h"
#include "bindless_texture_registry.h"

namespace ocarina {
class DescriptorSetLayout;
class DescriptorSet;
class Camera;
class CommandBuffer;
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

    /// Hard-coded FRAME set (set 0): global_ubo / g_textures / g_samplers / g_transforms.
    [[nodiscard]] DescriptorSetLayout* frame_descriptor_set_layout() const noexcept {
        return frame_descriptor_set_layout_;
    }

    [[nodiscard]] DescriptorSet* global_descriptor_set() const noexcept {
        return global_descriptor_set_;
    }

    DescriptorSet* get_global_descriptor_set(uint64_t name_id) const;
    DescriptorSet* get_global_descriptor_set(const std::string& name) const;

    /// Record that pipeline layout uses FRAME set 0 (already created in initialize()).
    void ensure_global_descriptor_sets(RHIPipelineLayout* pipeline_layout);

    /// Bind the FRAME global descriptor set at set 0.
    void bind_global_descriptor_sets(CommandBuffer& cmd, RHIPipelineLayout* pipeline_layout);

    /// Queue a bindless descriptor write (safe from loader / GPU-resource threads).
    /// Flushed on the render thread in update_per_frame().
    void queue_bindless_texture_update(uint32_t index, Texture* texture);

    void update_bindless_texture_at_index(uint32_t index, Texture* texture) {
        queue_bindless_texture_update(index, texture);
    }

    /// Queue a material texture / sampler / uniform-buffer update (safe from any thread).
    void queue_material_update(MaterialUpdateRequest request);

    [[nodiscard]] bool is_global_descriptor_set_index(uint32_t set_index) const noexcept {
        return set_index == static_cast<uint32_t>(DescriptorSetIndex::FRAME_SET)
            && global_descriptor_set_ != nullptr;
    }

    [[nodiscard]] GlobalUniformBuffer& global_uniform_buffer() noexcept { return global_ubo_; }
    [[nodiscard]] const GlobalUniformBuffer& global_uniform_buffer() const noexcept { return global_ubo_; }

    [[nodiscard]] TypedBuffer<GlobalUniformBuffer>& global_ubo_buffer() noexcept { return global_ubo_buffer_; }
    [[nodiscard]] const TypedBuffer<GlobalUniformBuffer>& global_ubo_buffer() const noexcept { return global_ubo_buffer_; }

    [[nodiscard]] TypedBuffer<GPUTransform>& transform_buffer() noexcept { return transform_buffer_; }
    [[nodiscard]] const TypedBuffer<GPUTransform>& transform_buffer() const noexcept { return transform_buffer_; }

    void set_sun_direction(const float3& direction) noexcept;
    void set_sun_color(const float3& color) noexcept;
    void set_sun_intensity(float intensity) noexcept;
    void set_light_position(const float3& position) noexcept;

    void set_update_callback(UpdateCallback cb) {
        update_ = std::move(cb);
    }

    void update_per_frame(double dt, Camera* camera = nullptr);

    void release_gpu_buffers();

private:
    FrameResources() = default;
    ~FrameResources() { release_gpu_buffers(); }

    void upload_global_uniform_buffer(Camera* camera);
    void upload_transform_buffer();
    void flush_pending_bindless_updates();
    void process_material_update();
    void create_global_descriptor_set();
    void create_default_gpu_buffers();
    void grow_transform_gpu_buffer(size_t element_count);
    void bind_global_ubo_if_needed();
    void bind_transform_storage_buffer_if_needed();

    Device* device_ = nullptr;
    DescriptorSetLayout* frame_descriptor_set_layout_ = nullptr;

    /// Single engine-owned FRAME descriptor set (set 0).
    DescriptorSet* global_descriptor_set_ = nullptr;
    /// Binding-name aliases that all resolve to `global_descriptor_set_`.
    std::unordered_map<uint64_t, DescriptorSet*> global_descriptor_sets_by_name_;

    struct PendingBindlessUpdate {
        uint32_t index = InvalidUI32;
        Texture* texture = nullptr;
    };
    ThreadSafeQueue<PendingBindlessUpdate> pending_bindless_updates_;

    ThreadSafeList<MaterialUpdateRequest> material_update_queue_;

    GlobalUniformBuffer global_ubo_{};
    TypedBuffer<GlobalUniformBuffer> global_ubo_buffer_{};
    bool global_ubo_descriptor_bound_ = false;
    TypedBuffer<GPUTransform> transform_buffer_{};
    bool transform_storage_descriptor_bound_ = false;
    UpdateCallback update_ = nullptr;
};

}// namespace ocarina

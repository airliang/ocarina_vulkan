#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/hash.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/pipeline_state.h"
#include "rhi/shader_base.h"
#include "rhi/resources/texture.h"
#include "rhi/resources/buffer.h"
#include "bindless_texture_registry.h"
#include <atomic>

namespace ocarina {
class DescriptorSetLayout;
class DescriptorSet;
class TextureSampler;
class Device;
class FrameResources;

class Material {
public:
    /// Convention: each material shader (mesh/water/particle/…) defines this struct for CPU layout.
    static constexpr const char* kMaterialParamsStructName = "MaterialParams";
    static constexpr const char* kMaterialsBufferName = "g_materials";

    enum class PropertyKind : uint8_t {
        /// Scalar / vector / matrix member inside a uniform buffer.
        UniformMember = 0,
        /// A uniform-buffer binding owned by this material.
        UniformBuffer,
    };

    struct MaterialProperty {
        string name;
        PropertyKind kind = PropertyKind::UniformMember;
        ShaderVariableType type = ShaderVariableType::FLOAT;
        uint32_t size = 0;
        uint32_t offset = 0;
        /// For UniformMember: parent UBO name id. For UniformBuffer: this binding's name id.
        uint64_t uniform_buffer_name_id = 0;
    };

    Material(Device* device, handle_ty vertex_shader, handle_ty pixel_shader);
    ~Material();

    /// Destroy owned GPU uniform buffers. Must run while the Device / VkDevice is still alive.
    void release_gpu_buffers();

    handle_ty get_vertex_shader() const { return pipeline_state_.shaders[0]; }
    handle_ty get_pixel_shader() const { return pipeline_state_.shaders[1]; }

    void set_property(uint64_t name_id, const void* data, size_t size);
    void set_property(const char* name, const void* data, size_t size) {
        set_property(hash64(name), data, size);
    }

    template<typename T>
    void set_property(uint64_t name_id, const T& value) {
        set_property(name_id, &value, sizeof(T));
    }

    template<typename T>
    void set_property(const char* name, const T& value) {
        set_property(hash64(name), value);
    }

    /// Bind a texture by handle (local descriptor set or bindless index member).
    /// Draw waits on is_renderable() until Texture::is_gpu_ready() for every bound handle.
    void set_property(uint64_t name_id, const TextureHandle& texture);
    void set_property(const char* name, const TextureHandle& texture) {
        set_property(hash64(name), texture);
    }

    /// True when every bound texture's GPU upload has completed on the upload timeline.
    /// Cached; re-evaluates only while textures_ready_ is still false.
    [[nodiscard]] bool is_renderable();

    void add_sampler(uint64_t name_id, const TextureSampler& sampler);
    void add_sampler(const char* name, const TextureSampler& sampler) {
        add_sampler(hash64(name), sampler);
    }

    /// Apply queued uniform-buffer uploads (render thread only).
    void apply_material_parameters_upload();

    void set_blend_state(const BlendState& blend_state) {
        if (blend_state != pipeline_state_.blend_state) {
            pipeline_state_.blend_state = blend_state;
            mark_pipeline_dirty();
        }
    }

    const BlendState& get_blend_state() const {
        return pipeline_state_.blend_state;
    }

    void set_raster_state(const RasterState& raster_state) {
        if (raster_state != pipeline_state_.raster_state) {
            pipeline_state_.raster_state = raster_state;
            mark_pipeline_dirty();
        }
    }

    const RasterState& get_raster_state() const {
        return pipeline_state_.raster_state;
    }

    void set_depth_stencil_state(const DepthStencilState& depth_stencil_state) {
        if (depth_stencil_state != pipeline_state_.depth_stencil_state) {
            pipeline_state_.depth_stencil_state = depth_stencil_state;
            mark_pipeline_dirty();
        }
    }

    const DepthStencilState& get_depth_stencil_state() const {
        return pipeline_state_.depth_stencil_state;
    }

    const PipelineState& get_pipeline_state() const {
        return pipeline_state_;
    }

    PipelineState& get_pipeline_state_mutable() {
        return pipeline_state_;
    }

    void mark_pipeline_dirty() {
        pipeline_dirty_ = true;
    }

    const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& descriptor_set_layouts() const {
        return descriptor_set_layouts_;
    }

    [[nodiscard]] bool is_pipeline_dirty() const { return pipeline_dirty_; }
    void clear_pipeline_dirty() { pipeline_dirty_ = false; }

    /// True when this material owns one or more local UBO properties.
    [[nodiscard]] bool has_material_uniform_buffer() const noexcept {
        return !uses_global_material_buffer_ && !uniform_buffers_.empty();
    }

    /// Shared `StructuredBuffer<MaterialParams> g_materials` on the global MATERIAL_SET.
    [[nodiscard]] bool uses_global_material_buffer() const noexcept {
        return uses_global_material_buffer_;
    }

    [[nodiscard]] uint32_t material_uniform_buffer_size() const noexcept {
        return material_params_byte_size_;
    }

    [[nodiscard]] uint64_t material_uniform_buffer_name_id() const noexcept {
        return material_params_buffer_name_id_;
    }

    [[nodiscard]] const std::vector<MaterialProperty>& material_properties() const noexcept {
        return material_properties_;
    }

    [[nodiscard]] const MaterialProperty* find_material_property(uint64_t name_id) const noexcept;

    [[nodiscard]] bool has_material_descriptor_set() const noexcept {
        return material_descriptor_set_ != nullptr;
    }

    [[nodiscard]] DescriptorSet* get_material_descriptor_set() const noexcept {
        return material_descriptor_set_;
    }

    [[nodiscard]] uint32_t material_descriptor_set_index() const noexcept {
        return material_descriptor_set_index_;
    }

    [[nodiscard]] DescriptorSetLayout* material_descriptor_set_layout() const noexcept {
        return material_descriptor_set_index_ != InvalidUI32
            ? descriptor_set_layouts_[material_descriptor_set_index_]
            : nullptr;
    }

    [[nodiscard]] bool is_material_descriptor_set_index(uint32_t set_index) const noexcept {
        return material_descriptor_set_ != nullptr && material_descriptor_set_index_ == set_index;
    }

    /// True when material_ubo lives on the same set as the global bindless array.
    [[nodiscard]] bool uses_shared_bindless_descriptor_set() const noexcept {
        return uses_shared_bindless_descriptor_set_;
    }

    void ensure_material_buffer();
    [[nodiscard]] bool has_material_buffer() const noexcept {
        return material_buffer_offset_ != InvalidUI32;
    }
    [[nodiscard]] uint32_t material_buffer_offset() const noexcept { return material_buffer_offset_; }
    [[nodiscard]] uint32_t material_buffer_size() const noexcept { return material_buffer_size_; }
    /// Slot index into `StructuredBuffer<MaterialParams> g_materials`.
    [[nodiscard]] uint32_t material_slot_index() const noexcept {
        if (material_buffer_offset_ == InvalidUI32 || material_buffer_size_ == 0) {
            return InvalidUI32;
        }
        return material_buffer_offset_ / material_buffer_size_;
    }

private:
    struct OwnedUniformBuffer {
        uint32_t size = 0;
        TypedBuffer<std::byte> buffer{};
        std::vector<uint8_t> cpu_data{};
        bool descriptor_bound = false;
        bool dirty = false;
    };

    void create_material_descriptor_set();
    void init_material_properties(handle_ty pixel_shader);
    void add_uniform_buffer_property(
        const char* binding_name,
        uint32_t buffer_size,
        const std::vector<RHIShader::UniformBufferMember>& members,
        bool create_owned_buffer);
    void apply_reflected_members(
        uint64_t buffer_name_id,
        uint32_t buffer_size,
        const std::vector<RHIShader::UniformBufferMember>& members);
    [[nodiscard]] bool detect_global_material_buffer_layout() const noexcept;
    void ensure_uniform_buffer_gpus();
    void upload_owned_uniform_buffer(uint64_t name_id, OwnedUniformBuffer& ubo);
    void queue_uniform_buffer_update();
    [[nodiscard]] OwnedUniformBuffer* find_owned_uniform_buffer(uint64_t name_id) noexcept;
    void bind_texture(uint64_t name_id, const TextureHandle& handle);
    [[nodiscard]] bool evaluate_textures_ready();
    [[nodiscard]] static Texture* resolve_texture_handle(const TextureHandle& handle) noexcept;

    DescriptorSetLayout *descriptor_set_layout_ = nullptr;
    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_layouts_ = {};
    PipelineState pipeline_state_;
    bool pipeline_dirty_ = true;

    Device* device_ = nullptr;

    /// Byte size / name of the primary params block (first local UBO or MaterialParams / g_materials).
    uint64_t material_params_buffer_name_id_ = 0;
    uint32_t material_params_byte_size_ = 0;

    std::vector<MaterialProperty> material_properties_;
    std::unordered_map<uint64_t, size_t> material_property_indices_;
    /// Owned GPU UBOs keyed by binding name id (local descriptor-set path only).
    std::unordered_map<uint64_t, OwnedUniformBuffer> uniform_buffers_;

    DescriptorSet* material_descriptor_set_ = nullptr;
    uint32_t material_descriptor_set_index_ = InvalidUI32;
    bool uses_shared_bindless_descriptor_set_ = false;
    bool uses_global_material_buffer_ = false;

    /// ECS staging region for the global `g_materials` path.
    uint32_t material_buffer_offset_ = InvalidUI32;
    uint32_t material_buffer_size_ = 0;

    /// Bound textures keyed by property / binding name id.
    std::unordered_map<uint64_t, TextureHandle> texture_handles_;
    std::atomic<bool> textures_ready_{true};

    /// True while a uniform-buffer update for this material is already queued.
    bool in_update_queue_ = false;

    /// Queue at most one pending uniform-buffer upload per material per frame batch.
    void try_queue_uniform_buffer_update();
    void clear_uniform_buffer_update_queued() noexcept { in_update_queue_ = false; }

    friend class FrameResources;
};

}// namespace ocarina

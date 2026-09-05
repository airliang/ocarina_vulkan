#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/hash.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/pipeline_state.h"
#include "rhi/shader_base.h"
#include "rhi/shader_program.h"
#include "rhi/resources/texture.h"
#include "rhi/resources/buffer.h"
#include "bindless_texture_registry.h"

namespace ocarina {
class DescriptorSetLayout;
class DescriptorSet;
class TextureSampler;
class Device;
class FrameResources;
class ShaderProgram;

class Material {
public:
    /// Convention: each material shader defines this struct for CPU layout.
    static constexpr const char* kMaterialParamsStructName = "MaterialParams";

    enum class PropertyKind : uint8_t {
        /// Scalar / vector / matrix member inside a uniform buffer.
        UniformMember = 0,
        /// A uniform-buffer binding owned by this material.
        UniformBuffer,
        /// Sampled / combined / storage image binding on the material set.
        Texture,
        /// Standalone sampler binding on the material set.
        Sampler,
        /// Storage-buffer binding on the material set.
        StorageBuffer,
    };

    struct MaterialProperty {
        string name;
        PropertyKind kind = PropertyKind::UniformMember;
        ShaderVariableType type = ShaderVariableType::FLOAT;
        ShaderBindingType binding_type = ShaderBindingType::UniformBuffer;
        uint32_t size = 0;
        uint32_t offset = 0;
        /// For UniformMember: parent UBO name id. For buffer bindings: this binding's name id.
        uint64_t uniform_buffer_name_id = 0;
        uint8_t binding = 0;
        uint8_t descriptor_set = 0;
    };

    Material(Device* device, ShaderProgram* shader_program);
    ~Material();

    /// Destroy owned GPU uniform buffers. Must run while the Device / VkDevice is still alive.
    void release_gpu_buffers();

    [[nodiscard]] ShaderProgram* get_shader_program() const noexcept { return shader_program_; }

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

    /// Bind a texture to a material-set texture property and queue a descriptor update.
    /// Bindless indices must be written separately via set_property on the UBO member.
    /// Draw waits on is_renderable() until every bound texture is GPU_Visible.
    void set_property(uint64_t name_id, const TextureHandle& texture);
    void set_property(const char* name, const TextureHandle& texture) {
        set_property(hash64(name), texture);
    }

    [[nodiscard]] bool is_renderable();
    [[nodiscard]] bool is_GPU_ready();

    /// Allocate descriptor set / GPU buffers when layouts are ready (render thread only).
    void try_finish_gpu_init();

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
        const_cast<Material*>(this)->ensure_gpu_shaders();
        return pipeline_state_;
    }

    PipelineState& get_pipeline_state_mutable() {
        ensure_gpu_shaders();
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

    [[nodiscard]] bool has_material_uniform_buffer() const noexcept {
        return !uniform_buffers_.empty();
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
        return material_descriptor_set_layout_;
    }

    [[nodiscard]] bool is_material_descriptor_set_index(uint32_t set_index) const noexcept {
        return material_descriptor_set_ != nullptr && material_descriptor_set_index_ == set_index;
    }

private:
    struct OwnedUniformBuffer {
        uint32_t size = 0;
        TypedBuffer<std::byte> buffer{};
        std::vector<uint8_t> cpu_data{};
        bool descriptor_bound = false;
        bool dirty = false;
    };

    void init_material_properties(ShaderProgram* shader_program);
    void resolve_material_descriptor_layout();
    void create_material_descriptor_set();
    void ensure_gpu_shaders();
    void add_binding_property(
        const ShaderVariableBinding& binding,
        PropertyKind kind);
    void add_uniform_buffer_property(
        const char* binding_name,
        uint32_t buffer_size,
        const std::vector<RHIShader::UniformBufferMember>& members,
        bool create_owned_buffer,
        uint8_t binding = 0,
        uint8_t descriptor_set = 0);
    void apply_reflected_members(
        uint64_t buffer_name_id,
        uint32_t buffer_size,
        const std::vector<RHIShader::UniformBufferMember>& members);
    void ensure_uniform_buffer_gpus();
    void upload_owned_uniform_buffer(uint64_t name_id, OwnedUniformBuffer& ubo);
    void queue_uniform_buffer_update();
    [[nodiscard]] OwnedUniformBuffer* find_owned_uniform_buffer(uint64_t name_id) noexcept;
    [[nodiscard]] bool requires_local_descriptor_set_layout() const noexcept;
    [[nodiscard]] bool is_material_infrastructure_ready() const noexcept;
    [[nodiscard]] DescriptorSet* find_descriptor_set_by_property_name(uint64_t name_id) const noexcept;

    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_layouts_ = {};
    DescriptorSetLayout* material_descriptor_set_layout_ = nullptr;
    PipelineState pipeline_state_;
    bool pipeline_dirty_ = true;

    Device* device_ = nullptr;
    ShaderProgram* shader_program_ = nullptr;

    uint64_t material_params_buffer_name_id_ = 0;
    uint32_t material_params_byte_size_ = 0;

    std::vector<MaterialProperty> material_properties_;
    std::unordered_map<uint64_t, size_t> material_property_indices_;
    std::unordered_map<uint64_t, OwnedUniformBuffer> uniform_buffers_;

    DescriptorSet* material_descriptor_set_ = nullptr;
    uint32_t material_descriptor_set_index_ = InvalidUI32;

    std::unordered_map<uint64_t, TextureHandle> texture_handles_;

    bool in_update_queue_ = false;

    void clear_uniform_buffer_update_queued() noexcept { in_update_queue_ = false; }

    friend class FrameResources;
};

}// namespace ocarina

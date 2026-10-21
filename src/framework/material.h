#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/hash.h"
#include "pass_group_id.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/pipeline_state.h"
#include "rhi/shader_base.h"
#include "rhi/shader_program.h"
#include "rhi/resources/texture.h"
#include "rhi/resources/cubemap.h"
#include "rhi/resources/buffer.h"
#include "bindless_texture_registry.h"
#include <mutex>

namespace ocarina {
class DescriptorSetLayout;
class DescriptorSet;
class TextureSampler;
class Device;
class FrameResources;
class ShaderProgram;
class LoadingProgressListener;
class RHIRenderPass;
struct MaterialAsset;

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

    /// One resolved technique binding for a PassGroupId from MaterialAsset::pass_participation.
    /// Owns the ShaderProgram used by that pass (programs are cached in ResourceManager).
    struct PassBinding {
        PassGroupId group = PassGroupId::Opaque;
        ocarina_string technique_name;
        ShaderProgram* shader_program = nullptr;
        PipelineState pipeline_state{};
        RHIRenderPass* render_pass = nullptr;
        bool pipeline_dirty = true;
    };

    /// Empty material; call initialize_from_asset to attach techniques / programs.
    explicit Material(Device* device);

    /// Legacy / fullscreen: single Opaque technique with the given program.
    Material(Device* device, ShaderProgram* shader_program);
    ~Material();

    /// Build PassBindings (programs + PSO state) from the asset, merge+validate
    /// non-FRAME descriptor layouts across all technique programs, reflect parameters
    /// from the richest material set, and enqueue PipelineCompileTasks for bound passes.
    bool initialize_from_asset(
        const MaterialAsset& asset,
        LoadingProgressListener* progress_listener = nullptr);

    /// Destroy owned GPU uniform buffers. Must run while the Device / VkDevice is still alive.
    void release_gpu_buffers();

    [[nodiscard]] ShaderProgram* shader_program_for_pass(PassGroupId group_id) const noexcept;

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

    /// Bind a cubemap to a material-set TextureCube property and queue a descriptor update.
    void set_cubemap(uint64_t name_id, Cubemap* cubemap);
    void set_cubemap(const char* name, Cubemap* cubemap) {
        set_cubemap(hash64(name), cubemap);
    }

    /// Track a bindless texture dependency for is_renderable() (does not write UBO or local descriptors).
    /// Pair with set_property(name_id, &texture.bindless_index_, ...) for the shader index.
    void set_bindless_texture(uint64_t name_id, const TextureHandle& texture);
    void set_bindless_texture(const char* name, const TextureHandle& texture) {
        set_bindless_texture(hash64(name), texture);
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
    /// Returns false when a newer CPU write arrived during the upload; the caller
    /// must leave the material in the update queue.
    [[nodiscard]] bool apply_material_parameters_upload();

    void set_blend_state(PassGroupId group_id, const BlendState& blend_state);
    [[nodiscard]] const BlendState& get_blend_state(PassGroupId group_id) const;

    /// Enable standard alpha blending (SRC_ALPHA / ONE_MINUS_SRC_ALPHA) and disable depth writes.
    void set_alpha_blend_enabled(PassGroupId group_id, bool enabled);

    /// True when pass_participation includes Transparent (exclusive forward bucket).
    [[nodiscard]] bool is_transparent() const noexcept;

    [[nodiscard]] bool has_pass_bindings() const noexcept { return !pass_bindings_.empty(); }

    /// True if MaterialAsset pass_participation includes this PassGroupId.
    [[nodiscard]] bool participates_in_pass(PassGroupId group_id) const noexcept;

    /// Pipeline state for a declared pass. Null if the material does not participate.
    [[nodiscard]] const PipelineState* pipeline_state_for_pass(PassGroupId group_id) const noexcept;
    [[nodiscard]] bool is_pass_pipeline_dirty(PassGroupId group_id) const noexcept;
    void clear_pass_pipeline_dirty(PassGroupId group_id) noexcept;

    void set_raster_state(PassGroupId group_id, const RasterState& raster_state);
    [[nodiscard]] const RasterState& get_raster_state(PassGroupId group_id) const;

    void set_depth_stencil_state(PassGroupId group_id, const DepthStencilState& depth_stencil_state);
    [[nodiscard]] const DepthStencilState& get_depth_stencil_state(PassGroupId group_id) const;

    void mark_pipeline_dirty();

    const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& descriptor_set_layouts() const {
        return descriptor_set_layouts_;
    }

    [[nodiscard]] bool has_material_uniform_buffer() const noexcept {
        return !uniform_buffers_.empty();
    }

    [[nodiscard]] uint32_t material_uniform_buffer_size() const noexcept {
        return material_params_byte_size_;
    }

    [[nodiscard]] uint64_t material_uniform_buffer_name_id() const noexcept {
        return material_params_buffer_name_id_;
    }

    [[nodiscard]] const ocarina_vector<MaterialProperty>& material_properties() const noexcept {
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
        ocarina_vector<uint8_t> cpu_data{};
        bool descriptor_bound = false;
        bool dirty = false;
    };

    void init_from_shader_program(ShaderProgram* shader_program);
    void init_material_properties(ShaderProgram* shader_program);
    /// Union non-FRAME set bindings from every PassBinding program. Errors if the same
    /// binding name maps to different indices (or types) across programs in one set.
    /// Creates/caches merged layouts and assigns them to every technique program.
    [[nodiscard]] bool collect_and_share_merged_descriptor_layouts() noexcept;
    [[nodiscard]] ShaderProgram* select_reflection_shader_program() const noexcept;
    void resolve_material_descriptor_layout();
    void create_material_descriptor_set();
    void ensure_gpu_shaders();
    void add_binding_property(
        const ShaderVariableBinding& binding,
        PropertyKind kind);
    void add_uniform_buffer_property(
        const char* binding_name,
        uint32_t buffer_size,
        const ocarina_vector<RHIShader::UniformBufferMember>& members,
        bool create_owned_buffer,
        uint8_t binding = 0,
        uint8_t descriptor_set = 0);
    void apply_reflected_members(
        uint64_t buffer_name_id,
        uint32_t buffer_size,
        const ocarina_vector<RHIShader::UniformBufferMember>& members);
    void ensure_uniform_buffer_gpus();
    void queue_uniform_buffer_update();
    [[nodiscard]] OwnedUniformBuffer* find_owned_uniform_buffer(uint64_t name_id) noexcept;
    [[nodiscard]] bool requires_local_descriptor_set_layout() const noexcept;
    [[nodiscard]] bool is_material_infrastructure_ready() const noexcept;
    [[nodiscard]] DescriptorSet* find_descriptor_set_by_property_name(uint64_t name_id) const noexcept;

    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_layouts_ = {};
    DescriptorSetLayout* material_descriptor_set_layout_ = nullptr;
    [[nodiscard]] PassBinding* find_pass_binding(PassGroupId group_id) noexcept;
    [[nodiscard]] const PassBinding* find_pass_binding(PassGroupId group_id) const noexcept;

    ocarina_vector<PassBinding> pass_bindings_;

    Device* device_ = nullptr;

    uint64_t material_params_buffer_name_id_ = 0;
    uint32_t material_params_byte_size_ = 0;

    ocarina_vector<MaterialProperty> material_properties_;
    ocarina_unordered_map<uint64_t, size_t> material_property_indices_;
    ocarina_unordered_map<uint64_t, OwnedUniformBuffer> uniform_buffers_;

    DescriptorSet* material_descriptor_set_ = nullptr;
    uint32_t material_descriptor_set_index_ = InvalidUI32;

    ocarina_unordered_map<uint64_t, TextureHandle> texture_handles_;
    ocarina_unordered_map<uint64_t, Cubemap*> cubemap_handles_;

    bool in_update_queue_ = false;
    /// Guards cpu_data, dirty, and in_update_queue_ against the loader thread.
    std::mutex uniform_mutex_;

    friend class FrameResources;
};

}// namespace ocarina

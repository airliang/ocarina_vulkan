#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/hash.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/pipeline_state.h"
#include "rhi/shader_base.h"
#include "rhi/resources/texture.h"

namespace ocarina {
class DescriptorSetLayout;
class DescriptorSet;
class TextureSampler;
class Device;

class Material {
public:
    static constexpr const char* kMaterialUniformBufferName = "material_ubo";

    struct MaterialProperty {
        string name;
        ShaderVariableType type = ShaderVariableType::FLOAT;
        uint32_t size = 0;
        uint32_t offset = 0;
    };

    Material(Device* device, handle_ty vertex_shader, handle_ty pixel_shader);
    ~Material() {}

    handle_ty get_vertex_shader() const { return pipeline_state_.shaders[0]; }
    handle_ty get_pixel_shader() const { return pipeline_state_.shaders[1]; }

    struct TextureHandle {
        uint32_t bindless_index_ = InvalidUI32;
        Texture* texture_ = nullptr;
    };

    void add_bindless_texture(uint64_t name_id, Texture* texture);
    TextureHandle get_bindless_texture_handle(uint64_t name_id) const;

    void add_texture(uint64_t name_id, Texture* texture);
    void add_sampler(uint64_t name_id, const TextureSampler& sampler);

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

    [[nodiscard]] bool has_material_uniform_buffer() const noexcept {
        return material_uniform_buffer_size_ > 0;
    }

    [[nodiscard]] uint32_t material_uniform_buffer_size() const noexcept {
        return material_uniform_buffer_size_;
    }

    [[nodiscard]] uint64_t material_uniform_buffer_name_id() const noexcept {
        return material_uniform_buffer_name_id_;
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

    void upload_material_uniform_buffer(const void* data, uint32_t size);

    void ensure_material_buffer();
    [[nodiscard]] bool has_material_buffer() const noexcept {
        return material_buffer_offset_ != InvalidUI32;
    }
    [[nodiscard]] uint32_t material_buffer_offset() const noexcept { return material_buffer_offset_; }
    [[nodiscard]] uint32_t material_buffer_size() const noexcept { return material_buffer_size_; }
    void mark_material_parameters_dirty() noexcept { material_parameters_dirty_ = true; }
    [[nodiscard]] bool material_parameters_dirty() const noexcept { return material_parameters_dirty_; }
    void clear_material_parameters_dirty() noexcept { material_parameters_dirty_ = false; }

private:
    void create_global_descriptor_sets();
    void create_material_descriptor_set();
    void init_material_properties(handle_ty pixel_shader);

    static uint32_t find_bindless_descriptor_set_index(
        const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts);
    static uint32_t find_global_ubo_descriptor_set_index(
        const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts);
    static uint32_t find_material_ubo_descriptor_set_index(
        const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts);
    static uint32_t find_max_descriptor_set_index(
        const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts);

    DescriptorSetLayout *descriptor_set_layout_ = nullptr;
    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_layouts_ = {};
    PipelineState pipeline_state_;
    bool pipeline_dirty_ = true;

    Device* device_ = nullptr;

    uint64_t material_uniform_buffer_name_id_ = 0;
    uint32_t material_uniform_buffer_size_ = 0;
    std::vector<MaterialProperty> material_properties_;
    std::unordered_map<uint64_t, size_t> material_property_indices_;

    DescriptorSet* material_descriptor_set_ = nullptr;
    uint32_t material_descriptor_set_index_ = InvalidUI32;

    uint32_t material_buffer_offset_ = InvalidUI32;
    uint32_t material_buffer_size_ = 0;
    bool material_parameters_dirty_ = true;

    std::unordered_map<uint64_t, TextureHandle> bindless_textures_;
};

}// namespace ocarina

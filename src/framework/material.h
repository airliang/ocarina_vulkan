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
struct RHIPipeline;
class TextureSampler;
class Device;
class RHIRenderPass;

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
        try_build_pipeline();
    }

    // Associate the render pass used for pipeline creation (main/setup thread).
    void set_target_render_pass(RHIRenderPass* render_pass);

    // Create or refresh the GPU pipeline for the current pipeline state and target render pass.
    // Intended to be called from the main/setup thread when pipeline state changes.
    void build_pipeline();

    RHIPipeline* get_pipeline() const { return pipeline_; }

    const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& descriptor_set_layouts() const {
        return descriptor_set_layouts_;
    }

    [[nodiscard]] RHIRenderPass* target_render_pass() const { return render_pass_; }
    [[nodiscard]] bool is_pipeline_dirty() const { return pipeline_dirty_; }
    [[nodiscard]] bool has_built_pipeline() const { return pipeline_ != nullptr; }

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

private:
    void try_build_pipeline();
    void create_global_descriptor_sets();
    void init_material_properties(handle_ty pixel_shader);

    static uint32_t find_bindless_descriptor_set_index(
        const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts);
    static uint32_t find_global_ubo_descriptor_set_index(
        const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts);
    static uint32_t find_max_descriptor_set_index(
        const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts);

    DescriptorSetLayout *descriptor_set_layout_ = nullptr;
    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_layouts_ = {};
    PipelineState pipeline_state_;
    RHIPipeline *pipeline_ = nullptr;
    RHIRenderPass *render_pass_ = nullptr;
    bool pipeline_dirty_ = true;

    Device* device_ = nullptr;
    std::mutex pipeline_mutex_;

    uint64_t material_uniform_buffer_name_id_ = 0;
    uint32_t material_uniform_buffer_size_ = 0;
    std::vector<MaterialProperty> material_properties_;
    std::unordered_map<uint64_t, size_t> material_property_indices_;
};

}// namespace ocarina

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/pipeline_state.h"
#include "rhi/resources/texture.h"

namespace ocarina {
class DescriptorSetLayout;
class DescriptorSet;
struct RHIPipeline;
template <class T>
class Shader;
class TextureSampler;
class Device;

class Material {
public:
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
            pipeline_dirty_ = true;
        }
        pipeline_state_.blend_state = blend_state;
    }

    const BlendState& get_blend_state() const {
        return pipeline_state_.blend_state;
    }

    void set_raster_state(const RasterState& raster_state) {
        if (raster_state != pipeline_state_.raster_state) {
            pipeline_dirty_ = true;
        }
        pipeline_state_.raster_state = raster_state;
    }

    const RasterState& get_raster_state() const {
        return pipeline_state_.raster_state;
    }

    void set_depth_stencil_state(const DepthStencilState& depth_stencil_state) {
        if (depth_stencil_state != pipeline_state_.depth_stencil_state) {
            pipeline_dirty_ = true;
        }
        pipeline_state_.depth_stencil_state = depth_stencil_state;
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

    RHIPipeline* get_pipeline() const { return pipeline_; }

    const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& descriptor_set_layouts() const {
        return descriptor_set_layouts_;
    }

    void update_material(RHIRenderPass* render_pass);

    bool is_pipeline_dirty() const { return pipeline_dirty_; }
private:
    void create_global_descriptor_sets();

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
    bool pipeline_dirty_ = true;

    Device* device_ = nullptr;
    std::mutex pipeline_mutex_;
};

}// namespace ocarina
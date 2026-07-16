//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/util.h"
#include "graphics_descriptions.h"

namespace ocarina {
template<typename T>
class Shader;
class VertexBuffer;
class DescriptorSetLayout;

struct RasterState {
    CullingMode cull_mode : 2;
    bool front_face : 1;
    bool depth_bias : 1;
    bool depth_clamp : 1;
    int padding : 27;

    static RasterState Default()
    {
        RasterState state{};
        state.cull_mode = CullingMode::BACK;
        state.front_face = false;// Counter-clockwise
        state.depth_bias = false;
        state.depth_clamp = false;
        return state;
    }

    bool operator==(const RasterState &other) const {
        return cull_mode == other.cull_mode &&
            front_face == other.front_face &&
            depth_bias == other.depth_bias &&
            depth_clamp == other.depth_clamp;
    }

    bool operator!=(const RasterState &other) const {
        return !(*this == other);
    }
};

struct BlendState {
    BlendFunction srccolorblend_factor : 5 = BlendFunction::ONE;
    BlendFunction dstcolorblend_factor : 5 = BlendFunction::ZERO;
    BlendFunction srcalphablend_factor : 5 = BlendFunction::ONE;
    BlendFunction dstalphablend_factor : 5 = BlendFunction::ZERO;
    BlendOperator colorBlendOp : 3 = BlendOperator::ADD;
    BlendOperator alphaBlendOp : 3 = BlendOperator::ADD;
    ColorMask color_mask : 4 = ColorMask::ColorMaskAll;///< Color mask for the blend state.
    bool blend_enable : 1 = false;
    int padding : 1;

    static BlendState Opaque()
    {
        BlendState state{};
        state.srccolorblend_factor = BlendFunction::ONE;
        state.dstcolorblend_factor = BlendFunction::ZERO;
        state.srcalphablend_factor = BlendFunction::ONE;
        state.dstalphablend_factor = BlendFunction::ZERO;
        state.colorBlendOp = BlendOperator::ADD;
        state.alphaBlendOp = BlendOperator::ADD;
        state.blend_enable = false;
        return state;
    }

    static BlendState AlphaBlend() {
        BlendState state{};
        state.srccolorblend_factor = BlendFunction::SRC_ALPHA;
        state.dstcolorblend_factor = BlendFunction::ONE_MINUS_SRC_ALPHA;
        state.srcalphablend_factor = BlendFunction::SRC_ALPHA;
        state.dstalphablend_factor = BlendFunction::ONE_MINUS_SRC_ALPHA;
        state.colorBlendOp = BlendOperator::ADD;
        state.alphaBlendOp = BlendOperator::ADD;
        state.blend_enable = true;
        return state;
    }

    bool operator==(const BlendState &other) const {
            return srccolorblend_factor == other.srccolorblend_factor &&
                dstcolorblend_factor == other.dstcolorblend_factor &&
                srcalphablend_factor == other.srcalphablend_factor &&
                dstalphablend_factor == other.dstalphablend_factor &&
                colorBlendOp == other.colorBlendOp &&
                alphaBlendOp == other.alphaBlendOp &&
                color_mask == other.color_mask &&
                blend_enable == other.blend_enable;
    }

    bool operator!=(const BlendState &other) const {
        return !(*this == other);
    }
};

struct DepthStencilState {
    bool depth_test_enable : 1;
    bool depth_write_enable : 1;
    SamplerCompareFunc depth_compare_op : 4;
    bool depth_bounds_test_enable : 1;
    bool stencil_test_enable : 1;
    int32_t padding : 25;

    static DepthStencilState Default() {
        DepthStencilState state{};
        state.depth_test_enable = true;
        state.depth_write_enable = true;
        state.depth_compare_op = SamplerCompareFunc::L;
        state.depth_bounds_test_enable = false;
        state.stencil_test_enable = false;
        return state;
    }

    bool operator==(const DepthStencilState &other) const {
        return depth_test_enable == other.depth_test_enable &&
            depth_write_enable == other.depth_write_enable &&
            depth_compare_op == other.depth_compare_op &&
            depth_bounds_test_enable == other.depth_bounds_test_enable &&
            stencil_test_enable == other.stencil_test_enable;
    }

    bool operator!=(const DepthStencilState &other) const {
        return !(*this == other);
    }
};

struct MultiSampleState {
    MultiSampleCount sample_count : 3 = MultiSampleCount::SAMPLE_COUNT_1;///< Number of samples per pixel.
    bool alpha_to_coverage_enable : 1 = false; ///< Enable alpha to coverage.
    bool alpha_to_one_enable : 1 = false;      ///< Enable alpha to one.
    bool sample_shading_enable : 1 = false;
    bool shading_rate_image_enable : 1 = false;///< Enable shading rate image.
    uint32_t padding : 25 = 0;
    //uint32_t sample_mask : 32 = 0xFFFFFFFF;     ///< Sample mask for multisampling.
    
};

struct PipelineState {
static constexpr uint16_t MAX_SHADER_STAGE = 2;
handle_ty shaders[MAX_SHADER_STAGE];
    handle_ty descriptorset_layout = InvalidUI64;        
    //VertexBuffer *vertex_buffer = nullptr;             
    RasterState raster_state;//  4
    BlendState blend_state;
    DepthStencilState depth_stencil_state;  
    MultiSampleState multiple_sample_state;
    PrimitiveType primitive_type = PrimitiveType::TRIANGLES;

    

    bool operator!=(const PipelineState &other) const {
        return shaders[0] != other.shaders[0] ||
            shaders[1] != other.shaders[1] ||
            descriptorset_layout != other.descriptorset_layout ||
            //vertex_buffer != other.vertex_buffer ||
            raster_state.cull_mode != other.raster_state.cull_mode ||
            raster_state.front_face != other.raster_state.front_face ||
            raster_state.depth_bias != other.raster_state.depth_bias ||
            raster_state.depth_clamp != other.raster_state.depth_clamp ||
            blend_state.srccolorblend_factor != other.blend_state.srccolorblend_factor ||
            blend_state.dstcolorblend_factor != other.blend_state.dstcolorblend_factor ||
            blend_state.srcalphablend_factor != other.blend_state.srcalphablend_factor ||
            blend_state.dstalphablend_factor != other.blend_state.dstalphablend_factor ||
            blend_state.colorBlendOp != other.blend_state.colorBlendOp ||
            blend_state.alphaBlendOp != other.blend_state.alphaBlendOp ||
            blend_state.color_mask != other.blend_state.color_mask ||
            blend_state.blend_enable != other.blend_state.blend_enable ||
            depth_stencil_state.depth_test_enable != other.depth_stencil_state.depth_test_enable ||
            depth_stencil_state.depth_write_enable != other.depth_stencil_state.depth_write_enable ||
            depth_stencil_state.depth_compare_op != other.depth_stencil_state.depth_compare_op ||
            depth_stencil_state.depth_bounds_test_enable != other.depth_stencil_state.depth_bounds_test_enable ||
            depth_stencil_state.stencil_test_enable != other.depth_stencil_state.stencil_test_enable ||
            multiple_sample_state.sample_count != other.multiple_sample_state.sample_count ||
            multiple_sample_state.alpha_to_coverage_enable != other.multiple_sample_state.alpha_to_coverage_enable ||
            multiple_sample_state.alpha_to_one_enable != other.multiple_sample_state.alpha_to_one_enable ||
            multiple_sample_state.sample_shading_enable != other.multiple_sample_state.sample_shading_enable;

    }

    bool operator==(const PipelineState &other) const {
        return !(*this != other);
    }

    [[nodiscard]] static PipelineState MakeGraphicsDefault(
        handle_ty vertex_shader,
        handle_ty pixel_shader) noexcept {
        PipelineState state{};
        state.shaders[0] = vertex_shader;
        state.shaders[1] = pixel_shader;
        state.descriptorset_layout = InvalidUI64;
        state.raster_state = RasterState::Default();
        state.blend_state = BlendState::Opaque();
        state.depth_stencil_state = DepthStencilState::Default();
        state.primitive_type = PrimitiveType::TRIANGLES;
        return state;
    }

    // Canonical key for PSO caches: copies only compared fields into a zero-initialized state.
    [[nodiscard]] PipelineState ForCacheKey() const noexcept {
        PipelineState key{};
        key.shaders[0] = shaders[0];
        key.shaders[1] = shaders[1];
        key.descriptorset_layout = descriptorset_layout;
        key.raster_state.cull_mode = raster_state.cull_mode;
        key.raster_state.front_face = raster_state.front_face;
        key.raster_state.depth_bias = raster_state.depth_bias;
        key.raster_state.depth_clamp = raster_state.depth_clamp;
        key.blend_state.srccolorblend_factor = blend_state.srccolorblend_factor;
        key.blend_state.dstcolorblend_factor = blend_state.dstcolorblend_factor;
        key.blend_state.srcalphablend_factor = blend_state.srcalphablend_factor;
        key.blend_state.dstalphablend_factor = blend_state.dstalphablend_factor;
        key.blend_state.colorBlendOp = blend_state.colorBlendOp;
        key.blend_state.alphaBlendOp = blend_state.alphaBlendOp;
        key.blend_state.color_mask = blend_state.color_mask;
        key.blend_state.blend_enable = blend_state.blend_enable;
        key.depth_stencil_state.depth_test_enable = depth_stencil_state.depth_test_enable;
        key.depth_stencil_state.depth_write_enable = depth_stencil_state.depth_write_enable;
        key.depth_stencil_state.depth_compare_op = depth_stencil_state.depth_compare_op;
        key.depth_stencil_state.depth_bounds_test_enable = depth_stencil_state.depth_bounds_test_enable;
        key.depth_stencil_state.stencil_test_enable = depth_stencil_state.stencil_test_enable;
        key.multiple_sample_state.sample_count = multiple_sample_state.sample_count;
        key.multiple_sample_state.alpha_to_coverage_enable = multiple_sample_state.alpha_to_coverage_enable;
        key.multiple_sample_state.alpha_to_one_enable = multiple_sample_state.alpha_to_one_enable;
        key.multiple_sample_state.sample_shading_enable = multiple_sample_state.sample_shading_enable;
        key.primitive_type = primitive_type;
        return key;
    }
};

struct PipelineStateHash {
    size_t operator()(const PipelineState& state) const noexcept {
        const PipelineState key = state.ForCacheKey();
        size_t hash = 0;
        hash_combine(hash, key.shaders[0]);
        hash_combine(hash, key.shaders[1]);
        hash_combine(hash, key.descriptorset_layout);
        hash_combine(hash, static_cast<uint32_t>(key.raster_state.cull_mode));
        hash_combine(hash, static_cast<uint32_t>(key.raster_state.front_face));
        hash_combine(hash, static_cast<uint32_t>(key.raster_state.depth_bias));
        hash_combine(hash, static_cast<uint32_t>(key.raster_state.depth_clamp));
        hash_combine(hash, static_cast<uint32_t>(key.blend_state.srccolorblend_factor));
        hash_combine(hash, static_cast<uint32_t>(key.blend_state.dstcolorblend_factor));
        hash_combine(hash, static_cast<uint32_t>(key.blend_state.srcalphablend_factor));
        hash_combine(hash, static_cast<uint32_t>(key.blend_state.dstalphablend_factor));
        hash_combine(hash, static_cast<uint32_t>(key.blend_state.colorBlendOp));
        hash_combine(hash, static_cast<uint32_t>(key.blend_state.alphaBlendOp));
        hash_combine(hash, static_cast<uint32_t>(key.blend_state.color_mask));
        hash_combine(hash, static_cast<uint32_t>(key.blend_state.blend_enable));
        hash_combine(hash, static_cast<uint32_t>(key.depth_stencil_state.depth_test_enable));
        hash_combine(hash, static_cast<uint32_t>(key.depth_stencil_state.depth_write_enable));
        hash_combine(hash, static_cast<uint32_t>(key.depth_stencil_state.depth_compare_op));
        hash_combine(hash, static_cast<uint32_t>(key.depth_stencil_state.depth_bounds_test_enable));
        hash_combine(hash, static_cast<uint32_t>(key.depth_stencil_state.stencil_test_enable));
        hash_combine(hash, static_cast<uint32_t>(key.multiple_sample_state.sample_count));
        hash_combine(hash, static_cast<uint32_t>(key.multiple_sample_state.alpha_to_coverage_enable));
        hash_combine(hash, static_cast<uint32_t>(key.multiple_sample_state.alpha_to_one_enable));
        hash_combine(hash, static_cast<uint32_t>(key.multiple_sample_state.sample_shading_enable));
        hash_combine(hash, static_cast<uint32_t>(key.primitive_type));
        return hash;
    }
};

struct PushConstantVariable {
    size_t offset;
    size_t size;
};

struct PipelineLayoutPushConstantRange {
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t shader_stage_flags = 0;
};

struct PipelineLayoutDesc {
    static constexpr uint8_t MAX_PUSH_CONSTANT_RANGES = 8;

    handle_ty shaders[PipelineState::MAX_SHADER_STAGE] = {};
    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_layouts = {};
    uint8_t descriptor_set_count = 0;
    std::array<PipelineLayoutPushConstantRange, MAX_PUSH_CONSTANT_RANGES> push_constant_ranges = {};
    uint8_t push_constant_count = 0;
};

struct RHIPipelineLayout {
    handle_ty handle = InvalidUI64;
    uint32_t push_constant_size = 0;
    uint32_t push_constant_shader_stage_flags = 0;
    std::unordered_map<uint64_t, PushConstantVariable> push_constant_variables_;
    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_layouts_ = {};
};

struct RHIPipeline
{
    handle_ty pipeline_layout = InvalidUI64;
    uint32_t push_constant_size = 0;
    std::unordered_map<uint64_t, PushConstantVariable> push_constant_variables_;
};

}// namespace ocarina
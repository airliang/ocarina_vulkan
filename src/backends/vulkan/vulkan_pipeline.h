#pragma once
#include "core/header.h"
#include "core/stl.h"
#include "core/util.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/pipeline_state.h"
#include <vulkan/vulkan.h>
#include <functional>
#include "vulkan_buffer.h"

namespace ocarina {

class VulkanShader;
class VulkanDevice;
struct VulkanVertexStreamBinding;

struct PushConstantRange
{
    uint32_t offset_ = 0;
    uint32_t size_ = 0;
    VkPipelineStageFlags shader_stages_ = 0;
};

struct VulkanPipelineLayout : public RHIPipelineLayout {
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

struct VulkanPipeline : public RHIPipeline {
    VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipelineStageFlags push_constant_shader_stages_ = 0;

    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_layouts_;

    /// Vertex input layout from the pipeline's vertex shader; used by set_vertex_buffer after bind_pipeline.
    const VulkanVertexStreamBinding* vertex_stream_binding_ = nullptr;

    const VulkanVertexStreamBinding* vertex_stream_binding() const noexcept
    {
        return vertex_stream_binding_;
    }
};

struct VertexInputAttributeDescription {
    VertexInputAttributeDescription &operator=(const VkVertexInputAttributeDescription &that) {
        location = that.location;
        binding = that.binding;
        format = that.format;
        offset = that.offset;
        return *this;
    }
    operator VkVertexInputAttributeDescription() const {
        return {location, binding, VkFormat(format), offset};
    }

    bool operator==(const VertexInputAttributeDescription &other) const {
        return location == other.location && binding == other.binding && format == other.format && offset == other.offset;
    }

    bool operator!=(const VertexInputAttributeDescription &other) const {
        return !(*this == other);
    }
    uint8_t location = 0;
    uint8_t binding = 0;
    uint16_t format = 0;
    uint32_t offset = 0;
};

struct VertexInputBindingDescription {
    VertexInputBindingDescription &operator=(const VkVertexInputBindingDescription &that) {
        binding = that.binding;
        stride = that.stride;
        inputRate = that.inputRate;
        return *this;
    }
    operator VkVertexInputBindingDescription() const {
        return {binding, stride, (VkVertexInputRate)inputRate};
    }

    bool operator==(const VkVertexInputBindingDescription &other) const {
        return binding == other.binding && inputRate == other.inputRate && stride == other.stride;
    }

    bool operator!=(const VkVertexInputBindingDescription &other) const {
        return !(*this == other);
    }

    uint16_t binding = 0;
    uint16_t inputRate = 0;
    uint32_t stride = 0;
};

struct PipelineKey
{
    static constexpr uint16_t MAX_VERTEX_ATTRIBUTES = 16;
    static constexpr uint16_t MAX_SHADER_STAGE = 2;
    static constexpr uint8_t MAX_COLOR_ATTACHMENTS = RenderPassCreation::MAX_COLOR_ATTACHMENTS;
    VkShaderModule shaders[MAX_SHADER_STAGE] = {VK_NULL_HANDLE};
    VkRenderPass render_pass = VK_NULL_HANDLE;
    uint8_t dynamic_color_attachment_count = 0;
    VkFormat dynamic_color_formats[MAX_COLOR_ATTACHMENTS] = {};
    VkFormat dynamic_depth_format = VK_FORMAT_UNDEFINED;
    RasterState raster_state = {};
    BlendState blend_state = {};
    DepthStencilState depth_stencil_state = {};
    VkPipelineLayout pipeline_layout;
    VkPrimitiveTopology topology;
    VertexInputAttributeDescription vertex_input_attributes[MAX_VERTEX_ATTRIBUTES];
    VertexInputBindingDescription vertex_input_binding[MAX_VERTEX_ATTRIBUTES];
    MultiSampleState multi_sample_state = {};

    bool operator==(const PipelineKey &other) const {
        bool states_comp = *(int *)&raster_state == *(int *)&other.raster_state &&
               render_pass == other.render_pass &&
               dynamic_color_attachment_count == other.dynamic_color_attachment_count &&
               dynamic_depth_format == other.dynamic_depth_format &&
               shaders[0] == other.shaders[0] &&
               shaders[1] == other.shaders[1] &&
               *(int *)&blend_state == *(int *)&other.blend_state &&
               *(int *)&depth_stencil_state == *(int *)&other.depth_stencil_state &&
            topology == other.topology &&
            pipeline_layout == other.pipeline_layout &&
            *(int *)&multi_sample_state == *(int *)&other.multi_sample_state;

        if (states_comp)
        {
            for (uint8_t i = 0; i < dynamic_color_attachment_count; ++i) {
                if (dynamic_color_formats[i] != other.dynamic_color_formats[i]) {
                    return false;
                }
            }
            for (uint8_t i = 0; i < MAX_VERTEX_ATTRIBUTES; ++i) {
                if (vertex_input_attributes[i] != other.vertex_input_attributes[i] ||
                    vertex_input_binding[i] != other.vertex_input_binding[i]) {
                    return false;
                }
            }
        }
        
        return states_comp;
    }
};

VulkanPipelineLayout* create_vulkan_pipeline_layout(VulkanDevice* device, const PipelineLayoutDesc& desc);
void destroy_vulkan_pipeline_layout(VulkanDevice* device, VulkanPipelineLayout* layout) noexcept;
bool build_vulkan_pipeline_layout_desc(const handle_ty shaders[PipelineState::MAX_SHADER_STAGE], PipelineLayoutDesc& out_desc) noexcept;

struct DynamicRenderingFormats {
    uint32_t color_attachment_count = 0;
    VkFormat color_formats[RenderPassCreation::MAX_COLOR_ATTACHMENTS] = {};
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
};

VulkanPipeline* create_vulkan_graphics_pipeline(
    const PipelineState& pipeline_state,
    VulkanDevice* device,
    VkRenderPass render_pass,
    RHIPipelineLayout* pipeline_layout,
    const DynamicRenderingFormats* dynamic_formats = nullptr);

void destroy_vulkan_graphics_pipeline(VulkanDevice* device, VulkanPipeline* pipeline) noexcept;

}// namespace ocarina

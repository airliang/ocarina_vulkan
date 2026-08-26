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

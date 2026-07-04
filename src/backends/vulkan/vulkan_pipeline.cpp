
#include "vulkan_device.h"
#include "vulkan_pipeline.h"
#include "util.h"
#include "vulkan_shader.h"
#include "vulkan_vertex_buffer.h"
#include "vulkan_driver.h"
#include "vulkan_descriptorset.h"

#include <algorithm>

namespace ocarina {

namespace {

uint32_t merge_push_constants(
    VulkanShader* vertex_shader,
    VulkanShader* pixel_shader,
    std::array<PushConstant, PipelineLayoutDesc::MAX_PUSH_CONSTANT_RANGES>& push_constant_merges)
{
    const std::vector<PushConstant>& vertex_push_constants = vertex_shader->get_push_constants();
    const std::vector<PushConstant>& pixel_push_constants = pixel_shader->get_push_constants();

    uint32_t push_constant_count = 0;
    for (const auto& vpc : vertex_push_constants) {
        push_constant_merges[push_constant_count++] = vpc;
    }

    for (const auto& ppc : pixel_push_constants) {
        bool found = false;
        for (size_t i = 0; i < push_constant_count; ++i) {
            if (ppc == push_constant_merges[i]) {
                found = true;
                push_constant_merges[i].stage_flags |= ppc.stage_flags;
                break;
            }
        }
        if (!found) {
            push_constant_merges[push_constant_count++] = ppc;
        }
    }
    return push_constant_count;
}

void fill_push_constant_variables(
    const std::array<PushConstant, PipelineLayoutDesc::MAX_PUSH_CONSTANT_RANGES>& push_constant_merges,
    uint32_t push_constant_num,
    RHIPipelineLayout& layout)
{
    uint32_t push_constant_offset = 0;
    for (uint32_t i = 0; i < push_constant_num; ++i) {
        const auto& pc = push_constant_merges[i];
        for (const auto& variable : pc.shader_variables) {
            PushConstantVariable pc_variable;
            pc_variable.offset = variable.offset + push_constant_offset;
            layout.push_constant_variables_.insert(std::make_pair(hash64(variable.name), pc_variable));
        }
        push_constant_offset += pc.size;
    }
}

void fill_pipeline_key_from_state(
    PipelineKey& pipeline_key,
    const PipelineState& pipeline_state,
    VkRenderPass render_pass,
    VkPipelineLayout pipeline_layout,
    const DynamicRenderingFormats* dynamic_formats)
{
    for (int i = 0; i < PipelineKey::MAX_SHADER_STAGE; ++i) {
        VulkanShader* shader = reinterpret_cast<VulkanShader*>(pipeline_state.shaders[i]);
        pipeline_key.shaders[i] = shader != nullptr ? shader->shader_module() : VK_NULL_HANDLE;
    }

    pipeline_key.blend_state = pipeline_state.blend_state;
    pipeline_key.depth_stencil_state = pipeline_state.depth_stencil_state;
    pipeline_key.raster_state = pipeline_state.raster_state;
    pipeline_key.multi_sample_state = pipeline_state.multiple_sample_state;
    pipeline_key.topology = get_vulkan_topology(pipeline_state.primitive_type);
    pipeline_key.render_pass = render_pass;
    pipeline_key.pipeline_layout = pipeline_layout;
    pipeline_key.dynamic_color_attachment_count = 0;
    pipeline_key.dynamic_depth_format = VK_FORMAT_UNDEFINED;
    if (dynamic_formats != nullptr) {
        pipeline_key.dynamic_color_attachment_count = static_cast<uint8_t>(dynamic_formats->color_attachment_count);
        for (uint32_t i = 0; i < dynamic_formats->color_attachment_count; ++i) {
            pipeline_key.dynamic_color_formats[i] = dynamic_formats->color_formats[i];
        }
        pipeline_key.dynamic_depth_format = dynamic_formats->depth_format;
    }

    VulkanShader* vertex_shader = reinterpret_cast<VulkanShader*>(pipeline_state.shaders[0]);
    if (vertex_shader != nullptr && vertex_shader->get_vertex_attribute_count() > 0) {
        const VulkanVertexStreamBinding& vertex_binding = vertex_shader->get_vertex_stream_binding();
        const uint8_t attr_count = static_cast<uint8_t>(vertex_binding.attribute_descriptions_.size());
        const uint8_t bind_count = static_cast<uint8_t>(vertex_binding.binding_descriptions_.size());
        for (size_t i = 0; i < PipelineKey::MAX_VERTEX_ATTRIBUTES; ++i) {
            if (i < attr_count) {
                pipeline_key.vertex_input_attributes[i] = vertex_binding.attribute_descriptions_[i];
            } else {
                pipeline_key.vertex_input_attributes[i] = {};
            }
            if (i < bind_count) {
                pipeline_key.vertex_input_binding[i] = vertex_binding.binding_descriptions_[i];
            } else {
                pipeline_key.vertex_input_binding[i] = {};
            }
        }
    } else {
        for (size_t i = 0; i < PipelineKey::MAX_VERTEX_ATTRIBUTES; ++i) {
            pipeline_key.vertex_input_attributes[i] = {};
            pipeline_key.vertex_input_binding[i] = {};
        }
    }
}

}// namespace

bool build_vulkan_pipeline_layout_desc(
    const handle_ty shaders[PipelineState::MAX_SHADER_STAGE],
    PipelineLayoutDesc& out_desc) noexcept
{
    VulkanShader* vertex_shader = reinterpret_cast<VulkanShader*>(shaders[0]);
    VulkanShader* pixel_shader = reinterpret_cast<VulkanShader*>(shaders[1]);
    if (vertex_shader == nullptr || pixel_shader == nullptr) {
        return false;
    }

    out_desc.shaders[0] = shaders[0];
    out_desc.shaders[1] = shaders[1];

    VulkanShader* shader_ptrs[2] = {vertex_shader, pixel_shader};
    out_desc.descriptor_set_layouts = VulkanDriver::instance().create_descriptor_set_layout(shader_ptrs, 2);

    uint32_t max_set_index = 0;
    bool has_any_layout = false;
    for (uint32_t i = 0; i < MAX_DESCRIPTOR_SETS_PER_SHADER; ++i) {
        if (out_desc.descriptor_set_layouts[i] != nullptr) {
            max_set_index = i;
            has_any_layout = true;
        }
    }
    out_desc.descriptor_set_count = has_any_layout ? static_cast<uint8_t>(max_set_index + 1) : 0;

    std::array<PushConstant, PipelineLayoutDesc::MAX_PUSH_CONSTANT_RANGES> push_constant_merges = {};
    const uint32_t push_constant_num = merge_push_constants(vertex_shader, pixel_shader, push_constant_merges);

    out_desc.push_constant_count = static_cast<uint8_t>(push_constant_num);
    for (uint32_t i = 0; i < push_constant_num; ++i) {
        out_desc.push_constant_ranges[i].offset = push_constant_merges[i].offset;
        out_desc.push_constant_ranges[i].size = push_constant_merges[i].size;
        out_desc.push_constant_ranges[i].shader_stage_flags = push_constant_merges[i].stage_flags;
    }

    std::sort(
        out_desc.push_constant_ranges.begin(),
        out_desc.push_constant_ranges.begin() + out_desc.push_constant_count,
        [](const PipelineLayoutPushConstantRange& lhs, const PipelineLayoutPushConstantRange& rhs) {
            if (lhs.offset != rhs.offset) {
                return lhs.offset < rhs.offset;
            }
            if (lhs.size != rhs.size) {
                return lhs.size < rhs.size;
            }
            return lhs.shader_stage_flags < rhs.shader_stage_flags;
        });

    return true;
}

VulkanPipelineLayout* create_vulkan_pipeline_layout(VulkanDevice* device, const PipelineLayoutDesc& desc)
{
    if (device == nullptr) {
        return nullptr;
    }

    VkDescriptorSetLayout vk_descriptor_set_layouts[MAX_DESCRIPTOR_SETS_PER_SHADER] = {};
    for (uint8_t i = 0; i < desc.descriptor_set_count; ++i) {
        auto* layout = static_cast<VulkanDescriptorSetLayout*>(desc.descriptor_set_layouts[i]);
        vk_descriptor_set_layouts[i] = layout != nullptr ? layout->layout() : VK_NULL_HANDLE;
    }

    std::array<VkPushConstantRange, PipelineLayoutDesc::MAX_PUSH_CONSTANT_RANGES> push_constant_ranges = {};
    uint32_t push_constant_size = 0;
    VkPipelineStageFlags push_constant_shader_stages = 0;
    for (uint8_t i = 0; i < desc.push_constant_count; ++i) {
        push_constant_ranges[i].offset = desc.push_constant_ranges[i].offset;
        push_constant_ranges[i].size = desc.push_constant_ranges[i].size;
        push_constant_ranges[i].stageFlags = desc.push_constant_ranges[i].shader_stage_flags;
        push_constant_size += desc.push_constant_ranges[i].size;
        push_constant_shader_stages |= desc.push_constant_ranges[i].shader_stage_flags;
    }

    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = desc.descriptor_set_count;
    pipeline_layout_info.pSetLayouts = vk_descriptor_set_layouts;
    pipeline_layout_info.pPushConstantRanges = push_constant_ranges.data();
    pipeline_layout_info.pushConstantRangeCount = desc.push_constant_count;

    auto* layout_entry = ocarina::new_with_allocator<VulkanPipelineLayout>();
    layout_entry->descriptor_set_layouts_ = desc.descriptor_set_layouts;
    layout_entry->push_constant_size = push_constant_size;
    layout_entry->push_constant_shader_stage_flags = push_constant_shader_stages;

    VulkanShader* vertex_shader = reinterpret_cast<VulkanShader*>(desc.shaders[0]);
    VulkanShader* pixel_shader = reinterpret_cast<VulkanShader*>(desc.shaders[1]);
    if (vertex_shader != nullptr && pixel_shader != nullptr) {
        std::array<PushConstant, PipelineLayoutDesc::MAX_PUSH_CONSTANT_RANGES> push_constant_merges = {};
        const uint32_t push_constant_num = merge_push_constants(vertex_shader, pixel_shader, push_constant_merges);
        fill_push_constant_variables(push_constant_merges, push_constant_num, *layout_entry);
    }

    VK_CHECK_RESULT(vkCreatePipelineLayout(
        device->logicalDevice(),
        &pipeline_layout_info,
        nullptr,
        &layout_entry->layout_));
    layout_entry->handle = reinterpret_cast<handle_ty>(layout_entry->layout_);

    return layout_entry;
}

void destroy_vulkan_pipeline_layout(VulkanDevice* device, VulkanPipelineLayout* layout) noexcept
{
    if (device == nullptr || layout == nullptr) {
        return;
    }

    if (layout->layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device->logicalDevice(), layout->layout_, nullptr);
        layout->layout_ = VK_NULL_HANDLE;
    }

    ocarina::delete_with_allocator(layout);
}

VulkanPipeline* create_vulkan_graphics_pipeline(
    const PipelineState& pipeline_state,
    VulkanDevice* device,
    VkRenderPass render_pass,
    RHIPipelineLayout* pipeline_layout,
    const DynamicRenderingFormats* dynamic_formats)
{
    VulkanShader* vertex_shader = reinterpret_cast<VulkanShader*>(pipeline_state.shaders[0]);
    VulkanShader* pixel_shader = reinterpret_cast<VulkanShader*>(pipeline_state.shaders[1]);
    auto* vulkan_pipeline_layout = static_cast<VulkanPipelineLayout*>(pipeline_layout);
    if (vertex_shader == nullptr || pixel_shader == nullptr || vulkan_pipeline_layout == nullptr) {
        return nullptr;
    }

    const VulkanVertexStreamBinding* vertex_binding = nullptr;
    if (vertex_shader->get_vertex_attribute_count() > 0) {
        vertex_binding = &vertex_shader->get_vertex_stream_binding();
    }

    PipelineKey pipeline_key{};
    fill_pipeline_key_from_state(
        pipeline_key,
        pipeline_state,
        render_pass,
        vulkan_pipeline_layout->layout_,
        dynamic_formats);

    const bool use_dynamic_rendering = dynamic_formats != nullptr && dynamic_formats->color_attachment_count > 0;
    const uint32_t color_attachment_count = use_dynamic_rendering ? dynamic_formats->color_attachment_count : 1u;

    VulkanPipeline* pipeline_entry = ocarina::new_with_allocator<VulkanPipeline>();
    pipeline_entry->push_constant_size = vulkan_pipeline_layout->push_constant_size;
    pipeline_entry->push_constant_shader_stages_ = vulkan_pipeline_layout->push_constant_shader_stage_flags;
    pipeline_entry->push_constant_variables_ = vulkan_pipeline_layout->push_constant_variables_;
    pipeline_entry->descriptor_set_layouts_ = vulkan_pipeline_layout->descriptor_set_layouts_;
    pipeline_entry->vertex_stream_binding_ = vertex_binding;

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {};
    vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (vertex_binding != nullptr) {
        vertex_input_state.vertexBindingDescriptionCount = static_cast<uint32_t>(vertex_binding->binding_descriptions_.size());
        vertex_input_state.pVertexBindingDescriptions = vertex_binding->binding_descriptions_.data();
        vertex_input_state.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_binding->attribute_descriptions_.size());
        vertex_input_state.pVertexAttributeDescriptions = vertex_binding->attribute_descriptions_.data();
    }

    VkPipelineShaderStageCreateInfo shader_stages[2];
    shader_stages[0] = VkPipelineShaderStageCreateInfo{};
    shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].pName = vertex_shader->get_entry_point();
    shader_stages[0].module = pipeline_key.shaders[0];

    shader_stages[1] = VkPipelineShaderStageCreateInfo{};
    shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].pName = pixel_shader->get_entry_point();
    shader_stages[1].module = pipeline_key.shaders[1];

    VkPipelineMultisampleStateCreateInfo multi_sample_state = {};
    multi_sample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multi_sample_state.alphaToCoverageEnable = pipeline_key.multi_sample_state.alpha_to_coverage_enable ? VK_TRUE : VK_FALSE;
    multi_sample_state.alphaToOneEnable = pipeline_key.multi_sample_state.alpha_to_one_enable ? VK_TRUE : VK_FALSE;
    multi_sample_state.sampleShadingEnable = pipeline_key.multi_sample_state.sample_shading_enable ? VK_TRUE : VK_FALSE;
    multi_sample_state.minSampleShading = 1.0f;
    multi_sample_state.pSampleMask = nullptr;
    multi_sample_state.rasterizationSamples = get_vulkan_sample_count_flag_bit(pipeline_key.multi_sample_state.sample_count);
    multi_sample_state.flags = 0;

    VkPipelineColorBlendAttachmentState blend_attachment_states[RenderPassCreation::MAX_COLOR_ATTACHMENTS] = {};
    for (uint32_t i = 0; i < color_attachment_count; ++i) {
        VkPipelineColorBlendAttachmentState& blend_attachment_state = blend_attachment_states[i];
        blend_attachment_state.blendEnable = pipeline_key.blend_state.blend_enable ? VK_TRUE : VK_FALSE;
        blend_attachment_state.colorWriteMask = get_vulkan_color_component_flag_bits(pipeline_key.blend_state.color_mask);
        blend_attachment_state.alphaBlendOp = get_vulkan_blend_op(pipeline_key.blend_state.alphaBlendOp);
        blend_attachment_state.colorBlendOp = get_vulkan_blend_op(pipeline_key.blend_state.colorBlendOp);
        blend_attachment_state.srcAlphaBlendFactor = get_vulkan_blend_factor(pipeline_key.blend_state.srcalphablend_factor);
        blend_attachment_state.dstAlphaBlendFactor = get_vulkan_blend_factor(pipeline_key.blend_state.dstalphablend_factor);
        blend_attachment_state.srcColorBlendFactor = get_vulkan_blend_factor(pipeline_key.blend_state.srccolorblend_factor);
        blend_attachment_state.dstColorBlendFactor = get_vulkan_blend_factor(pipeline_key.blend_state.dstcolorblend_factor);
    }

    VkPipelineColorBlendStateCreateInfo color_blend_state = {};
    color_blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.attachmentCount = color_attachment_count;
    color_blend_state.logicOpEnable = VK_FALSE;
    color_blend_state.logicOp = VK_LOGIC_OP_COPY;
    color_blend_state.pAttachments = blend_attachment_states;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {};
    depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state.depthCompareOp = get_vulkan_compare_op(pipeline_key.depth_stencil_state.depth_compare_op);
    depth_stencil_state.depthTestEnable = pipeline_key.depth_stencil_state.depth_test_enable ? VK_TRUE : VK_FALSE;
    depth_stencil_state.depthWriteEnable = pipeline_key.depth_stencil_state.depth_write_enable ? VK_TRUE : VK_FALSE;
    if (use_dynamic_rendering && dynamic_formats->depth_format != VK_FORMAT_UNDEFINED) {
        depth_stencil_state.depthTestEnable = VK_TRUE;
    }

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization_state = {};
    rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.cullMode = get_vulkan_cull_mode(pipeline_key.raster_state.cull_mode);
    rasterization_state.frontFace = get_vulkan_front_face(pipeline_key.raster_state.front_face);
    rasterization_state.depthBiasEnable = pipeline_key.raster_state.depth_bias ? VK_TRUE : VK_FALSE;
    rasterization_state.depthClampEnable = pipeline_key.raster_state.depth_clamp ? VK_TRUE : VK_FALSE;
    rasterization_state.depthBiasSlopeFactor = 0.0f;
    rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization_state.lineWidth = 1.0f;
    rasterization_state.depthBiasConstantFactor = 0.0f;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {};
    input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology = static_cast<VkPrimitiveTopology>(pipeline_key.topology);
    input_assembly_state.primitiveRestartEnable = VK_FALSE;

    VkPipelineDynamicStateCreateInfo pipeline_dynamic_state_create_info{};
    pipeline_dynamic_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    std::array<VkDynamicState, 2> dynamic_state_enables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    pipeline_dynamic_state_create_info.pDynamicStates = dynamic_state_enables.data();
    pipeline_dynamic_state_create_info.dynamicStateCount = static_cast<uint32_t>(dynamic_state_enables.size());
    pipeline_dynamic_state_create_info.flags = 0;

    VkGraphicsPipelineCreateInfo pipeline_create_info = {};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_create_info.layout = pipeline_key.pipeline_layout;
    pipeline_create_info.renderPass = use_dynamic_rendering ? VK_NULL_HANDLE : pipeline_key.render_pass;
    pipeline_create_info.subpass = 0;
    pipeline_create_info.stageCount = 2;
    pipeline_create_info.pStages = shader_stages;
    pipeline_create_info.pVertexInputState = &vertex_input_state;
    pipeline_create_info.pMultisampleState = &multi_sample_state;
    pipeline_create_info.pColorBlendState = &color_blend_state;
    pipeline_create_info.pDepthStencilState = &depth_stencil_state;
    pipeline_create_info.pViewportState = &viewport_state;
    pipeline_create_info.pRasterizationState = &rasterization_state;
    pipeline_create_info.pInputAssemblyState = &input_assembly_state;
    pipeline_create_info.pDynamicState = &pipeline_dynamic_state_create_info;
    pipeline_create_info.basePipelineIndex = -1;
    pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;

    VkPipelineRenderingCreateInfo rendering_create_info{};
    if (use_dynamic_rendering) {
        rendering_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        rendering_create_info.colorAttachmentCount = dynamic_formats->color_attachment_count;
        rendering_create_info.pColorAttachmentFormats = dynamic_formats->color_formats;
        rendering_create_info.depthAttachmentFormat = dynamic_formats->depth_format;
        rendering_create_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
        pipeline_create_info.pNext = &rendering_create_info;
    }

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(
        device->logicalDevice(),
        VK_NULL_HANDLE,
        1,
        &pipeline_create_info,
        nullptr,
        &pipeline_entry->pipeline_));
    pipeline_entry->pipeline_layout_ = vulkan_pipeline_layout->layout_;
    pipeline_entry->pipeline_layout = vulkan_pipeline_layout->handle;

    return pipeline_entry;
}

void destroy_vulkan_graphics_pipeline(VulkanDevice* device, VulkanPipeline* pipeline) noexcept {
    if (device == nullptr || pipeline == nullptr) {
        return;
    }

    if (pipeline->pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device->logicalDevice(), pipeline->pipeline_, nullptr);
        pipeline->pipeline_ = VK_NULL_HANDLE;
    }

    ocarina::delete_with_allocator(pipeline);
}

}// namespace ocarina

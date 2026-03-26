//
// Created by Zero on 06/08/2022.
//

#pragma once

#include "vulkan_renderpass.h"
#include "vulkan_device.h"
#include "util.h"
#include "vulkan_driver.h"
#include "rhi/pipeline_state.h"
#include "vulkan_pipeline.h"
#include "vulkan_rendertarget.h"
#include "vulkan_index_buffer.h"
#include "vulkan_vertex_buffer.h"
#include "vulkan_descriptorset_writer.h"
#include "vulkan_command_buffer.h"

namespace ocarina {

VulkanRenderPass::VulkanRenderPass(VulkanDevice *device, const RenderPassCreation &render_pass_creation) : RHIRenderPass(render_pass_creation), device_(device) {
    render_target_count_ = render_pass_creation.render_target_count;
    
    for (uint8_t i = 0; i < render_target_count_; ++i) {
        render_target_[i] = ocarina::new_with_allocator<VulkanRenderTarget>(device, render_pass_creation.render_targets[i]);
        clear_values_[i].color = { {render_pass_creation.render_targets[i].clear_color.x, render_pass_creation.render_targets[i].clear_color.y,
                                  render_pass_creation.render_targets[i].clear_color.z, 0} };
        clear_values_[i].depthStencil = {render_pass_creation.render_targets[i].clear_depth, render_pass_creation.render_targets[i].clear_stencil };
    }

    if (is_use_swapchain_framebuffer())
    {
        clear_values_[0].color = {{render_pass_creation.swapchain_clear_color.x, render_pass_creation.swapchain_clear_color.y,
                                  render_pass_creation.swapchain_clear_color.z, 0}};
        clear_values_[1].depthStencil = {render_pass_creation.swapchain_clear_depth, render_pass_creation.swapchain_clear_stencil};
    }

    setup_render_pass();
}

VulkanRenderPass::~VulkanRenderPass() {
    if (!is_use_swapchain_framebuffer() && render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_->logicalDevice(), render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
}

void VulkanRenderPass::begin_render_pass(const CommandBuffer& cmd) {
    clear_draw_call_items();
    if (begin_render_pass_callback_ != nullptr)
    {
        begin_render_pass_callback_(this);
    }
    
    // Implementation for beginning the render pass
    VulkanDriver& driver = VulkanDriver::instance();
    VkCommandBuffer current_buffer = reinterpret_cast<VkCommandBuffer>(cmd.command_buffer);//driver.get_current_command_buffer();
    command_buffer_ = reinterpret_cast<handle_ty>(current_buffer);
    VulkanCommandBuffer* vulkan_command_buffer = reinterpret_cast<VulkanCommandBuffer*>(cmd.impl());
    if (vulkan_command_buffer)
    {
        vulkan_command_buffer->set_pipeline_stage_flags(is_use_swapchain_framebuffer() ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    }

    VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = render_pass_;
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent.width = size_.x;
    renderPassBeginInfo.renderArea.extent.height = size_.y;
    renderPassBeginInfo.clearValueCount = is_use_swapchain_framebuffer() ? 2 : render_target_count_ + 1;
    renderPassBeginInfo.pClearValues = clear_values_;

    if (render_target_count_ == 0)
    {
        renderPassBeginInfo.framebuffer = driver.get_frame_buffer(driver.current_buffer());
    }
    else
    {
        renderPassBeginInfo.framebuffer = vulkan_frame_buffer_;
    }

    vkCmdBeginRenderPass(current_buffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {
        .x = viewport_.x,
        .y = viewport_.y,
        .width = viewport_.z,
        .height = viewport_.w,
        .minDepth = 0.0f,
        .maxDepth = 1.0f};
    vkCmdSetViewport(current_buffer, 0, 1, &viewport);

    VkRect2D scissor;
    scissor.offset = {scissor_.x, scissor_.y};
    scissor.extent = {(uint32_t)scissor_.z, (uint32_t)scissor_.w};
    vkCmdSetScissor(current_buffer, 0, 1, &scissor);
}

void VulkanRenderPass::end_render_pass(const CommandBuffer& cmd) {
    // Implementation for ending the render pass    
    VkCommandBuffer cmd_buffer = reinterpret_cast<VkCommandBuffer>(cmd.command_buffer);
    vkCmdEndRenderPass(cmd_buffer);
}

void VulkanRenderPass::draw_items(const CommandBuffer& cmd) {
    VulkanDriver& driver = VulkanDriver::instance();
    VkCommandBuffer cmd_buffer = reinterpret_cast<VkCommandBuffer>(cmd.command_buffer);
    for (const auto& queue : pipeline_render_queues_) {
        VulkanPipeline *vulkan_pipeline = static_cast<VulkanPipeline *>(queue.first);
        driver.bind_descriptor_sets(cmd_buffer, global_descriptor_sets_array_.data(), (uint32_t)DescriptorSetIndex::GLOBAL_SET, global_descriptor_sets_array_.size(), vulkan_pipeline->pipeline_layout_);
        driver.bind_pipeline(cmd_buffer , *vulkan_pipeline);

        for (auto &item : queue.second->draw_call_items) {
            if (item.pre_render_function) {
                item.pre_render_function(item);
            }
            if (item.push_constant_data) {
                driver.push_constants(cmd_buffer, vulkan_pipeline->pipeline_layout_, item.push_constant_data, item.push_constant_size, 0, vulkan_pipeline->push_constant_shader_stages_);
            }
            
            if (!item.descriptor_sets.empty())
            {
                driver.bind_descriptor_sets(cmd_buffer, item.descriptor_sets.data(), item.first_set, item.descriptor_sets.size(), vulkan_pipeline->pipeline_layout_);
            }
            //for (uint32_t i = 0; i < item.descriptor_set_count; ++i)
            //{
            //    DescriptorSetsBinding& descriptor_set_group = item.descriptor_sets_binding_group[i];
            //    driver.bind_descriptor_sets(descriptor_set_group.descriptor_sets.data(), descriptor_set_group.first_set,
            //        descriptor_set_group.descriptor_set_count, vulkan_pipeline->pipeline_layout_);
            //}

            VulkanVertexBuffer *vertex_buffer = static_cast<VulkanVertexBuffer *>(item.pipeline_state->vertex_buffer);
            VulkanShader *vertex_shader = reinterpret_cast<VulkanShader *>(item.pipeline_state->shaders[0]);//driver.get_shader(item.pipeline_state->shaders[0]);
            driver.set_vertex_buffer(cmd_buffer, *(vertex_buffer->get_or_create_vertex_binding(vertex_shader)));
            driver.draw_triangles(cmd_buffer, static_cast<VulkanIndexBuffer *>(item.index_buffer));
        }
    }
}

void VulkanRenderPass::setup_render_pass() {
    if (render_pass_ != VK_NULL_HANDLE) {
        return;
    }

    VkDevice device = device_->logicalDevice();

    if (is_use_swapchain_framebuffer())
    {
        //swap chain frame buffer as target
        VulkanSwapchain *swapChain = device_->get_swapchain();
        size_ = swapChain->resolution();
        scissor_ = {0, 0, (int)size_.x, (int)size_.y};
        viewport_ = {0, 0, (float)size_.x, (float)size_.y};
        render_pass_ = VulkanDriver::instance().get_framebuffer_render_pass();
    }
    else
    {
        for (uint8_t i = 0; i < render_target_count_; ++i)
        {
            //create attachments by rendertarget
        }
    }
}

}// namespace ocarina
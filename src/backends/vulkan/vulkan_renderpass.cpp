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
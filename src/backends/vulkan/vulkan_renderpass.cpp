//
// Created by Zero on 06/08/2022.
//

#include "vulkan_renderpass.h"
#include "vulkan_device.h"
#include "util.h"
#include "vulkan_driver.h"
#include "vulkan_texture.h"
#include "rhi/rendertarget.h"

namespace ocarina {

VulkanRenderPass::VulkanRenderPass(VulkanDevice *device, const RenderPassCreation &render_pass_creation)
    : RHIRenderPass(render_pass_creation), device_(device) {
    clear_values_[0].color = {{clear_color_.x, clear_color_.y, clear_color_.z, clear_color_.w}};
    clear_values_[1].depthStencil = {clear_depth_, clear_stencil_};

    if (is_offscreen_renderpass()) {
        const uint32_t color_count = color_attachment_count();
        for (uint32_t i = 0; i < color_count; ++i) {
            clear_values_[i].color = {{clear_color_.x, clear_color_.y, clear_color_.z, clear_color_.w}};
        }
        if (depth_attachment() != nullptr) {
            clear_values_[color_count].depthStencil = {clear_depth_, clear_stencil_};
        }
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

    if (is_use_swapchain_framebuffer()) {
        VulkanSwapchain *swapChain = device_->get_swapchain();
        size_ = swapChain->resolution();
        scissor_ = {0, 0, static_cast<int>(size_.x), static_cast<int>(size_.y)};
        viewport_ = {0, 0, static_cast<float>(size_.x), static_cast<float>(size_.y)};
        color_attachment_format_count_ = 1;
        color_attachment_formats_[0] = swapChain->color_format();
        depth_attachment_format_ = swapChain->depth_format();
        if (device_->supports_dynamic_rendering()) {
            render_pass_ = VK_NULL_HANDLE;
        } else {
            render_pass_ = VulkanDriver::instance().get_framebuffer_render_pass();
        }
        return;
    }

    if (color_attachment_count() > 0) {
        auto *color0 = static_cast<VulkanTexture *>(color_attachment(0)->impl());
        size_ = {color0->width(), color0->height()};
    } else {
        OC_ASSERT(depth_attachment() != nullptr);
        auto *depth0 = static_cast<VulkanTexture *>(depth_attachment()->impl());
        size_ = {depth0->width(), depth0->height()};
    }
    scissor_ = {0, 0, static_cast<int>(size_.x), static_cast<int>(size_.y)};
    viewport_ = {0, 0, static_cast<float>(size_.x), static_cast<float>(size_.y)};

    color_attachment_format_count_ = color_attachment_count();
    for (uint32_t i = 0; i < color_attachment_count(); ++i) {
        auto *texture = static_cast<VulkanTexture *>(color_attachment(i)->impl());
        color_attachment_formats_[i] = texture->vk_format();
    }

    if (depth_attachment() != nullptr) {
        auto *depth_texture = static_cast<VulkanTexture *>(depth_attachment()->impl());
        depth_attachment_format_ = depth_texture->vk_format();
    }
}

}// namespace ocarina

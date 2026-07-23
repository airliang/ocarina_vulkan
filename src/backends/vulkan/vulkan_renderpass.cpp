//
// Created by Zero on 06/08/2022.
//

#include "vulkan_renderpass.h"
#include "vulkan_device.h"
#include "util.h"
#include "vulkan_driver.h"
#include "vulkan_texture.h"

namespace ocarina {

VulkanRenderPass::VulkanRenderPass(VulkanDevice *device, const RenderPassCreation &render_pass_creation)
    : RHIRenderPass(render_pass_creation), device_(device) {
    clear_color_ = render_pass_creation.clear_color;
    clear_depth_ = render_pass_creation.clear_depth;
    clear_stencil_ = render_pass_creation.clear_stencil;
    swapchain_clear_color_ = render_pass_creation.swapchain_clear_color;
    swapchain_clear_depth_ = render_pass_creation.swapchain_clear_depth;
    swapchain_clear_stencil_ = render_pass_creation.swapchain_clear_stencil;

    color_attachment_count_ = render_pass_creation.color_attachment_count;
    for (uint32_t i = 0; i < color_attachment_count_; ++i) {
        color_attachments_[i] = render_pass_creation.color_attachments[i];
    }
    depth_attachment_ = render_pass_creation.depth_attachment;

    if (is_use_swapchain_framebuffer()) {
        clear_values_[0].color = {{swapchain_clear_color_.x, swapchain_clear_color_.y,
                                   swapchain_clear_color_.z, swapchain_clear_color_.w}};
        clear_values_[1].depthStencil = {swapchain_clear_depth_, swapchain_clear_stencil_};
    } else {
        for (uint32_t i = 0; i < color_attachment_count_; ++i) {
            clear_values_[i].color = {{clear_color_.x, clear_color_.y, clear_color_.z, clear_color_.w}};
        }
        if (depth_attachment_ != nullptr) {
            clear_values_[color_attachment_count_].depthStencil = {clear_depth_, clear_stencil_};
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

    if (color_attachment_count_ > 0) {
        auto *color0 = static_cast<VulkanTexture *>(color_attachments_[0]->impl());
        size_ = {color0->width(), color0->height()};
    } else {
        OC_ASSERT(depth_attachment_ != nullptr);
        auto *depth0 = static_cast<VulkanTexture *>(depth_attachment_->impl());
        size_ = {depth0->width(), depth0->height()};
    }
    scissor_ = {0, 0, static_cast<int>(size_.x), static_cast<int>(size_.y)};
    viewport_ = {0, 0, static_cast<float>(size_.x), static_cast<float>(size_.y)};

    color_attachment_format_count_ = color_attachment_count_;
    for (uint32_t i = 0; i < color_attachment_count_; ++i) {
        auto *texture = static_cast<VulkanTexture *>(color_attachments_[i]->impl());
        color_attachment_formats_[i] = texture->vk_format();
    }

    if (depth_attachment_ != nullptr) {
        auto *depth_texture = static_cast<VulkanTexture *>(depth_attachment_->impl());
        depth_attachment_format_ = depth_texture->vk_format();
    }
}

}// namespace ocarina

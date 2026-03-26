#include "vulkan_command_buffer.h"
#include "vulkan_device.h"
#include "vulkan_renderpass.h"
#include "vulkan_driver.h"

namespace ocarina {

VulkanCommandBuffer::VulkanCommandBuffer(VulkanDevice *device, VkCommandPool cmd_pool, VkCommandBuffer cmd_buffer)
    : device_(device), command_pool_(cmd_pool), vulkan_command_buffer_(cmd_buffer) {
}

VulkanCommandBuffer::~VulkanCommandBuffer() {
    // Cleanup if necessary
    if (vulkan_command_buffer_ != VK_NULL_HANDLE) {
        // Note: Command buffers are typically freed when the command pool is reset or destroyed,
        // so we don't need to explicitly free them here.
        vkFreeCommandBuffers(device_->logicalDevice(), command_pool_, 1, &vulkan_command_buffer_);
    }
}

void VulkanCommandBuffer::begin_render_pass(RHIRenderPass* render_pass) {
    if (render_pass->is_swapchain_renderpass()) {
        // Begin swapchain render pass
        pipeline_stage_flags_ = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else {
        // Begin offscreen render pass
    }

    VulkanRenderPass* vulkan_render_pass = static_cast<VulkanRenderPass*>(render_pass);

    VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = vulkan_render_pass->render_pass();
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent.width = render_pass->size().x;
    renderPassBeginInfo.renderArea.extent.height = render_pass->size().y;
    renderPassBeginInfo.clearValueCount = render_pass->is_use_swapchain_framebuffer() ? 2 : render_pass->render_target_count() + 1;
    renderPassBeginInfo.pClearValues = vulkan_render_pass->clear_values();

    if (render_pass->render_target_count() == 0)
    {
        renderPassBeginInfo.framebuffer = VulkanDriver::instance().get_frame_buffer(VulkanDriver::instance().current_buffer());
    }
    else
    {
        renderPassBeginInfo.framebuffer = vulkan_render_pass->vulkan_frame_buffer();
    }

    vkCmdBeginRenderPass(vulkan_command_buffer_, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanCommandBuffer::end_render_pass() {
    vkCmdEndRenderPass(vulkan_command_buffer_);
}
void VulkanCommandBuffer::bind_pipeline(handle_ty pipeline) {

}
void VulkanCommandBuffer::bind_descriptor_sets(DescriptorSet** descriptor_sets, uint32_t descriptor_set_count, RHIPipeline* pipeline) {

}

}// namespace ocarina



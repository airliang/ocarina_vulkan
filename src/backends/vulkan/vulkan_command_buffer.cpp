#include "vulkan_command_buffer.h"
#include "vulkan_device.h"
#include "vulkan_renderpass.h"
#include "vulkan_driver.h"
#include "vulkan_vertex_buffer.h"
#include "vulkan_pipeline.h"
#include "vulkan_descriptorset.h"
#include "vulkan_index_buffer.h"

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
    render_pass->clear_draw_call_items();
    render_pass->execute_begin_render_pass_callback();
    if (render_pass->is_swapchain_renderpass()) {
        // Begin swapchain render pass
        pipeline_stage_flags_ = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else {
        // Begin offscreen render pass
    }

    VulkanRenderPass* vulkan_render_pass = static_cast<VulkanRenderPass*>(render_pass);
    set_pipeline_stage_flags(vulkan_render_pass->is_use_swapchain_framebuffer() ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);

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
    float4 viewport_ = render_pass->viewport();
    VkViewport viewport = {
        .x = viewport_.x,
        .y = viewport_.y,
        .width = viewport_.z,
        .height = viewport_.w,
        .minDepth = 0.0f,
        .maxDepth = 1.0f };
    vkCmdSetViewport(vulkan_command_buffer_, 0, 1, &viewport);

    int4 scissor_ = render_pass->scissor();
    VkRect2D scissor;
    scissor.offset = { scissor_.x, scissor_.y };
    scissor.extent = { (uint32_t)scissor_.z, (uint32_t)scissor_.w };
    vkCmdSetScissor(vulkan_command_buffer_, 0, 1, &scissor);
}

void VulkanCommandBuffer::end_render_pass() {
    vkCmdEndRenderPass(vulkan_command_buffer_);
}
void VulkanCommandBuffer::bind_pipeline(const RHIPipeline* pipeline) {
    VulkanPipeline* vulkan_pipeline = static_cast<VulkanPipeline*>(const_cast<RHIPipeline*>(pipeline));
    OC_ASSERT(vulkan_pipeline != nullptr);
    if (current_pipeline_ != vulkan_pipeline) {
        vkCmdBindPipeline(vulkan_command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_pipeline->pipeline_);
        current_pipeline_ = vulkan_pipeline;
    }
    
}
void VulkanCommandBuffer::bind_descriptor_sets(DescriptorSet** descriptor_sets, uint32_t first_set, uint32_t descriptor_set_count, RHIPipeline* pipeline) {
    VulkanPipeline* vulkan_pipeline = static_cast<VulkanPipeline*>(pipeline);
    std::array<VkDescriptorSet, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_handles = { VK_NULL_HANDLE };
    for (uint32_t i = 0; i < descriptor_set_count; ++i) {
        descriptor_set_handles[i] = static_cast<VulkanDescriptorSet*>(descriptor_sets[i])->descriptor_set();
    }

    state_.dirty_mask = 0;

    for (uint32_t i = 0; i < descriptor_set_count; ++i) {
        uint32_t set_index = first_set + i;

        // Only mark as dirty if the handle has actually changed
        if (set_index < MAX_DESCRIPTOR_SETS_PER_SHADER) {
            if (state_.bound_sets[set_index] != descriptor_set_handles[i]) {
                state_.bound_sets[set_index] = descriptor_set_handles[i];
                state_.dirty_mask |= (1 << set_index);
            }
        }
    }

    if (state_.dirty_mask != 0) {
        for (uint32_t i = 0; i < MAX_DESCRIPTOR_SETS_PER_SHADER; ++i) {
            if (state_.dirty_mask & (1 << i)) {
                // Option A: Bind single set
                vkCmdBindDescriptorSets(vulkan_command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_pipeline->pipeline_layout_, i, 1, &state_.bound_sets[i], 0, nullptr);

                // Option B: For optimization, you could group contiguous bits 
                // into a single vkCmdBindDescriptorSets call.
            }
        }
    }
}

void VulkanCommandBuffer::draw_indexed(IndexBuffer* index_buffer, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) {
    VulkanIndexBuffer* vk_index_buffer = static_cast<VulkanIndexBuffer*>(index_buffer);
    vkCmdBindIndexBuffer(vulkan_command_buffer_, vk_index_buffer->buffer_handle(), 0, index_buffer->is_16_bit() ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(vulkan_command_buffer_, index_buffer->get_index_count(), 1, 0, 0, 0);
}

void VulkanCommandBuffer::push_constants(const void* data, uint32_t offset, uint32_t size) {
    OC_ASSERT(current_pipeline_ != nullptr);
    vkCmdPushConstants(vulkan_command_buffer_, current_pipeline_->pipeline_layout_, current_pipeline_->push_constant_shader_stages_, offset, size, data);
}

void VulkanCommandBuffer::draw_indirect(handle_ty indirect_buffer, uint32_t draw_count, uint32_t stride) {
}

void VulkanCommandBuffer::draw_indexed_indirect(handle_ty indirect_buffer, uint32_t draw_count, uint32_t stride) {
}

void VulkanCommandBuffer::set_vertex_buffer(VertexBuffer* vertex_buffer, handle_ty vertex_shader) {
    VulkanVertexBuffer* vk_vertex_buffer = static_cast<VulkanVertexBuffer*>(vertex_buffer);
    VulkanShader* vk_vertex_shader = reinterpret_cast<VulkanShader*>(vertex_shader);
    VulkanVertexStreamBinding* vertex_stream = vk_vertex_buffer->get_or_create_vertex_binding(vk_vertex_shader);
    vkCmdBindVertexBuffers(vulkan_command_buffer_, 0, vertex_stream->buffers_.size(), vertex_stream->buffers_.data(), vertex_stream->offsets_.data());
}

void VulkanCommandBuffer::reset() {
    current_pipeline_ = nullptr;
    state_.dirty_mask = 0;
    std::fill(std::begin(state_.bound_sets), std::end(state_.bound_sets), VK_NULL_HANDLE);
}

}// namespace ocarina



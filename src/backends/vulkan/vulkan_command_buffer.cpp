#include "vulkan_command_buffer.h"
#include "vulkan_device.h"
#include "vulkan_renderpass.h"
#include "vulkan_driver.h"
#include "vulkan_vertex_buffer.h"
#include "vulkan_pipeline.h"
#include "vulkan_shader.h"
#include "vulkan_descriptorset.h"
#include "vulkan_index_buffer.h"
#include "vulkan_fence.h"
#include "vulkan_buffer.h"
#include "vulkan_texture.h"
#include "vulkan_swapchain.h"
#include "util.h"
#include <stdexcept>

namespace ocarina {

VulkanCommandBuffer::VulkanCommandBuffer(VulkanDevice *device, VkCommandPool cmd_pool, VkCommandBuffer cmd_buffer, QueueType queue_type)
    : device_(device), command_pool_(cmd_pool), vulkan_command_buffer_(cmd_buffer), queue_type_(queue_type) {
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
    current_render_pass_ = render_pass;
    VulkanRenderPass* vulkan_render_pass = static_cast<VulkanRenderPass*>(render_pass);
    if (render_pass->is_offscreen_renderpass()) {
        begin_offscreen_render_pass(render_pass, vulkan_render_pass);
    } else {
        begin_swapchain_render_pass(render_pass, vulkan_render_pass);
    }

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
    scissor.extent = { static_cast<uint32_t>(scissor_.z), static_cast<uint32_t>(scissor_.w) };
    vkCmdSetScissor(vulkan_command_buffer_, 0, 1, &scissor);
}

void VulkanCommandBuffer::begin_swapchain_render_pass(RHIRenderPass* render_pass, VulkanRenderPass* vulkan_render_pass) {
    set_pipeline_stage_flags(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    if (device_->supports_dynamic_rendering()) {
        use_dynamic_rendering_ = true;

        VulkanSwapchain* swapchain = device_->get_swapchain();
        const uint32_t image_index = VulkanDriver::instance().current_buffer();
        const SwapChainBuffer buffer = swapchain->get_swapchain_buffer(static_cast<int>(image_index));
        const VulkanSwapchain::DepthStencil depth_stencil = swapchain->get_depth_stencil();

        image_layout_barrier(
            buffer.image_,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        VkImageAspectFlags depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (swapchain->depth_format() >= VK_FORMAT_D16_UNORM_S8_UINT) {
            depth_aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        image_layout_barrier(
            depth_stencil.image,
            depth_aspect,
            1,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        VkRenderingAttachmentInfo color_attachment{};
        color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attachment.imageView = buffer.imageView_;
        color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.clearValue = vulkan_render_pass->clear_values()[0];

        VkRenderingAttachmentInfo depth_attachment{};
        depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_attachment.imageView = depth_stencil.view;
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attachment.clearValue = vulkan_render_pass->clear_values()[1];

        VkRenderingInfo rendering_info{};
        rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering_info.renderArea.offset = {0, 0};
        rendering_info.renderArea.extent = {render_pass->size().x, render_pass->size().y};
        rendering_info.layerCount = 1;
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachments = &color_attachment;
        rendering_info.pDepthAttachment = &depth_attachment;
        rendering_info.pStencilAttachment = nullptr;

        vkCmdBeginRendering(vulkan_command_buffer_, &rendering_info);
        return;
    }

    use_dynamic_rendering_ = false;

    VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = vulkan_render_pass->render_pass();
    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = {render_pass->size().x, render_pass->size().y};
    renderPassBeginInfo.clearValueCount = 2;
    renderPassBeginInfo.pClearValues = vulkan_render_pass->clear_values();
    renderPassBeginInfo.framebuffer = VulkanDriver::instance().get_frame_buffer(VulkanDriver::instance().current_buffer());
    vkCmdBeginRenderPass(vulkan_command_buffer_, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanCommandBuffer::begin_offscreen_render_pass(RHIRenderPass* render_pass, VulkanRenderPass* vulkan_render_pass) {
    use_dynamic_rendering_ = true;
    if (render_pass->color_attachment_count() > 0) {
        set_pipeline_stage_flags(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    } else {
        set_pipeline_stage_flags(VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
    }

    for (uint32_t i = 0; i < render_pass->color_attachment_count(); ++i) {
        auto* texture = static_cast<VulkanTexture*>(render_pass->color_attachment(i)->impl());
        image_layout_barrier(texture, texture->vk_image_layout(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        texture->set_image_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }

    VkRenderingAttachmentInfo depth_attachment_info{};
    VkRenderingAttachmentInfo* depth_attachment_ptr = nullptr;
    if (render_pass->depth_attachment() != nullptr) {
        auto* depth_texture = static_cast<VulkanTexture*>(render_pass->depth_attachment()->impl());
        image_layout_barrier(depth_texture, depth_texture->vk_image_layout(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        depth_texture->set_image_layout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        depth_attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_attachment_info.imageView = depth_texture->vk_image_view();
        depth_attachment_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attachment_info.clearValue.depthStencil = vulkan_render_pass->clear_values()[render_pass->color_attachment_count()].depthStencil;
        depth_attachment_ptr = &depth_attachment_info;
    }

    VkRenderingAttachmentInfo color_attachments[RenderPassCreation::MAX_COLOR_ATTACHMENTS];
    for (uint32_t i = 0; i < render_pass->color_attachment_count(); ++i) {
        auto* texture = static_cast<VulkanTexture*>(render_pass->color_attachment(i)->impl());
        VkRenderingAttachmentInfo& color_attachment = color_attachments[i];
        color_attachment = {};
        color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attachment.imageView = texture->vk_image_view();
        color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.clearValue.color = vulkan_render_pass->clear_values()[i].color;
    }

    VkRenderingInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset = {0, 0};
    rendering_info.renderArea.extent = {render_pass->size().x, render_pass->size().y};
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = render_pass->color_attachment_count();
    rendering_info.pColorAttachments = color_attachments;
    rendering_info.pDepthAttachment = depth_attachment_ptr;
    rendering_info.pStencilAttachment = nullptr;

    vkCmdBeginRendering(vulkan_command_buffer_, &rendering_info);
}

void VulkanCommandBuffer::end_render_pass() {
    if (use_dynamic_rendering_) {
        RHIRenderPass* render_pass = current_render_pass_;
        vkCmdEndRendering(vulkan_command_buffer_);

        if (render_pass->is_swapchain_renderpass()) {
            VulkanSwapchain* swapchain = device_->get_swapchain();
            const uint32_t image_index = VulkanDriver::instance().current_buffer();
            const SwapChainBuffer buffer = swapchain->get_swapchain_buffer(static_cast<int>(image_index));
            image_layout_barrier(
                buffer.image_,
                VK_IMAGE_ASPECT_COLOR_BIT,
                1,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        } else {
            for (uint32_t i = 0; i < render_pass->color_attachment_count(); ++i) {
                auto* texture = static_cast<VulkanTexture*>(render_pass->color_attachment(i)->impl());
                if ((static_cast<uint32_t>(texture->usage_flags()) & static_cast<uint32_t>(TextureUsageFlags::ShaderReadOnly)) != 0) {
                    image_layout_barrier(texture, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    texture->set_image_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
            }

            if (render_pass->depth_attachment() != nullptr) {
                auto* depth_texture = static_cast<VulkanTexture*>(render_pass->depth_attachment()->impl());
                if ((static_cast<uint32_t>(depth_texture->usage_flags()) & static_cast<uint32_t>(TextureUsageFlags::ShaderReadOnly)) != 0) {
                    const VkImageLayout sample_layout = depth_texture->is_depth_stencil()
                        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    image_layout_barrier(depth_texture, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, sample_layout);
                    depth_texture->set_image_layout(sample_layout);
                }
            }
        }

        use_dynamic_rendering_ = false;
    } else {
        vkCmdEndRenderPass(vulkan_command_buffer_);
    }
    current_render_pass_ = nullptr;
}
void VulkanCommandBuffer::bind_pipeline(const RHIPipeline* pipeline) {
    VulkanPipeline* vulkan_pipeline = static_cast<VulkanPipeline*>(const_cast<RHIPipeline*>(pipeline));
    OC_ASSERT(vulkan_pipeline != nullptr);
    if (current_pipeline_ != vulkan_pipeline) {
        vkCmdBindPipeline(vulkan_command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_pipeline->pipeline_);
        current_pipeline_ = vulkan_pipeline;
    }
    
}
void VulkanCommandBuffer::bind_descriptor_sets(DescriptorSet** descriptor_sets, uint32_t first_set, uint32_t descriptor_set_count, handle_ty pipeline_layout_handle) {
    VkPipelineLayout pipeline_layout = reinterpret_cast<VkPipelineLayout>(pipeline_layout_handle);
    std::array<VkDescriptorSet, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_handles = { VK_NULL_HANDLE };
    for (uint32_t i = 0; i < descriptor_set_count; ++i) {
        descriptor_set_handles[i] = static_cast<VulkanDescriptorSet*>(descriptor_sets[i])->descriptor_set();
    }

    state_.dirty_mask = 0;

    for (uint32_t i = 0; i < descriptor_set_count; ++i) {
        uint32_t set_index = first_set + i;

        // Only mark as dirty if the handle has actually changed
        if (set_index < MAX_DESCRIPTOR_SETS_PER_SHADER) {
            if (state_.bound_sets[set_index] != descriptor_set_handles[i] ||
                state_.bound_set_layouts[set_index] != pipeline_layout) {
                state_.bound_sets[set_index] = descriptor_set_handles[i];
                state_.bound_set_layouts[set_index] = pipeline_layout;
                state_.dirty_mask |= (1 << set_index);
            }
        }
    }

    if (state_.dirty_mask != 0) {
        for (uint32_t i = 0; i < MAX_DESCRIPTOR_SETS_PER_SHADER; ++i) {
            if (state_.dirty_mask & (1 << i)) {
                // Option A: Bind single set
                vkCmdBindDescriptorSets(vulkan_command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, state_.bound_set_layouts[i], i, 1, &state_.bound_sets[i], 0, nullptr);

                // Option B: For optimization, you could group contiguous bits 
                // into a single vkCmdBindDescriptorSets call.
            }
        }
    }
}

void VulkanCommandBuffer::set_index_buffer(IndexBuffer* index_buffer, uint32_t first_index) {
    VulkanIndexBuffer* vk_index_buffer = static_cast<VulkanIndexBuffer*>(index_buffer);
    const VkDeviceSize byte_offset = static_cast<VkDeviceSize>(first_index)
        * (index_buffer->is_16_bit() ? sizeof(uint16_t) : sizeof(uint32_t));
    vkCmdBindIndexBuffer(
        vulkan_command_buffer_,
        vk_index_buffer->buffer_handle(),
        byte_offset,
        index_buffer->is_16_bit() ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void VulkanCommandBuffer::draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) {
    vkCmdDrawIndexed(
        vulkan_command_buffer_,
        index_count,
        instance_count,
        first_index,
        vertex_offset,
        first_instance);
}

void VulkanCommandBuffer::push_constants(const void* data, uint32_t offset, uint32_t size) {
    OC_ASSERT(current_pipeline_ != nullptr);
    vkCmdPushConstants(vulkan_command_buffer_, current_pipeline_->pipeline_layout_, current_pipeline_->push_constant_shader_stages_, offset, size, data);
}

void VulkanCommandBuffer::draw_indirect(handle_ty indirect_buffer, uint32_t draw_count, uint32_t stride) {
}

void VulkanCommandBuffer::draw_indexed_indirect(handle_ty indirect_buffer, uint32_t draw_count, uint32_t stride) {
}

void VulkanCommandBuffer::set_vertex_buffer(VertexBuffer* vertex_buffer, uint32_t base_vertex) {
    OC_ASSERT(current_pipeline_ != nullptr);
    const VulkanVertexStreamBinding* vertex_stream_binding = current_pipeline_->vertex_stream_binding();
    if (vertex_stream_binding == nullptr || vertex_stream_binding->attribute_descriptions_.empty()) {
        return;
    }
    VulkanVertexBuffer* vk_vertex_buffer = static_cast<VulkanVertexBuffer*>(vertex_buffer);
    for (size_t i = 0; i < vertex_stream_binding->attribute_descriptions_.size(); ++i) {
        VertexAttributeType::Enum attribute_type = vertex_stream_binding->attribute_types_[i];
        VertexStream* vertex_stream = vk_vertex_buffer->get_vertex_stream(attribute_type);
        if (vertex_stream == nullptr || vertex_stream->buffer == 0) {
            continue;
        }
        VkDeviceSize offset = vertex_stream_binding->offsets_[i]
            + static_cast<VkDeviceSize>(base_vertex) * vertex_stream->stride;
        VulkanBuffer* vk_buffer = reinterpret_cast<VulkanBuffer*>(vertex_stream->buffer);
        VkBuffer buffer_handle = vk_buffer->buffer_handle();
        vkCmdBindVertexBuffers(vulkan_command_buffer_, static_cast<uint32_t>(i), 1, &buffer_handle, &offset);
    }
}

void VulkanCommandBuffer::submit_to_queue(QueueType queue_type, Fence* fence) {
    // Submission logic will depend on how you manage command buffers and queues in your application.
    // Typically, you would end the command buffer recording here and submit it to the appropriate Vulkan queue.

    // Submit to the queue
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &vulkan_command_buffer_;
    VkQueue queue = VulkanDriver::instance().get_queue(queue_type);
    VkFence vk_fence = fence ? reinterpret_cast<VkFence>(fence->native_handle()) : VK_NULL_HANDLE;
    VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submit_info, vk_fence));

    if (fence) {
        // Optionally, you can wait for the fence here or let the caller handle it.
        vkWaitForFences(device_->logicalDevice(), 1, &vk_fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
    }
    // Wait for the fence to signal that command buffer has finished executing
    // 
    //VK_CHECK_RESULT(vkWaitForFences(device_->logicalDevice(), 1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max()));
    //vkDestroyFence(device_->logicalDevice(), fence, nullptr);
}

void VulkanCommandBuffer::begin() {
    VK_CHECK_RESULT(vkResetCommandBuffer(vulkan_command_buffer_, 0));

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin_info.pInheritanceInfo = nullptr;
    VK_CHECK_RESULT(vkBeginCommandBuffer(vulkan_command_buffer_, &begin_info));

    if (record_gpu_timestamps_ && queue_type_ == QueueType::Graphics) {
        VulkanDriver::instance().write_gpu_timestamp_begin(vulkan_command_buffer_);
    }
}

void VulkanCommandBuffer::end() {
    if (record_gpu_timestamps_ && queue_type_ == QueueType::Graphics) {
        VulkanDriver::instance().write_gpu_timestamp_end(vulkan_command_buffer_);
    }
    VK_CHECK_RESULT(vkEndCommandBuffer(vulkan_command_buffer_));
}

void VulkanCommandBuffer::set_viewport(float x, float y, float width, float height, float min_depth, float max_depth) {
    VkViewport viewport{};
    viewport.x = x;
    viewport.y = y;
    viewport.width = width;
    viewport.height = height;
    viewport.minDepth = min_depth;
    viewport.maxDepth = max_depth;
    vkCmdSetViewport(vulkan_command_buffer_, 0, 1, &viewport);
}

void VulkanCommandBuffer::set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    VkRect2D scissor{};
    scissor.offset = {x, y};
    scissor.extent = {width, height};
    vkCmdSetScissor(vulkan_command_buffer_, 0, 1, &scissor);
}

void VulkanCommandBuffer::reset() {
    current_pipeline_ = nullptr;
    state_.dirty_mask = 0;
    std::fill(std::begin(state_.bound_sets), std::end(state_.bound_sets), VK_NULL_HANDLE);
    std::fill(std::begin(state_.bound_set_layouts), std::end(state_.bound_set_layouts), VK_NULL_HANDLE);
}

void VulkanCommandBuffer::copy_buffer(VulkanBuffer* src, VulkanBuffer* dst)
{
    VkBufferCopy buffer_copy{};

    buffer_copy.size = src->size();

    vkCmdCopyBuffer(vulkan_command_buffer_, src->buffer_handle(), dst->buffer_handle(), 1, &buffer_copy);
}

void VulkanCommandBuffer::copy_image(VulkanBuffer* src, VulkanTexture* dst)
{
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = {dst->resolution().x, dst->resolution().y, 1};

    copy_image(src, dst, &region, 1);
}

void VulkanCommandBuffer::copy_image(VulkanBuffer* src, VulkanTexture* dst, const VkBufferImageCopy* regions, uint32_t region_count)
{
    VkImage image = reinterpret_cast<VkImage>(dst->tex_handle());

    vkCmdCopyBufferToImage(
        vulkan_command_buffer_,
        src->buffer_handle(),
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        region_count,
        regions);
}

void VulkanCommandBuffer::image_layout_barrier(VulkanTexture* texture, VkImageLayout old_layout, VkImageLayout new_layout)
{
    image_layout_barrier(
        reinterpret_cast<VkImage>(texture->tex_handle()),
        texture->vk_aspect_mask(),
        texture->mip_levels(),
        old_layout,
        new_layout);
}

void VulkanCommandBuffer::image_layout_barrier(VkImage image, VkImageAspectFlags aspect_mask, uint32_t mip_levels,
                                              VkImageLayout old_layout, VkImageLayout new_layout)
{
    if (old_layout == new_layout) {
        return;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect_mask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mip_levels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags source_stage{};
    VkPipelineStageFlags destination_stage{};

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        if (queue_type_ == QueueType::Copy) {
            barrier.dstAccessMask = 0;
            destination_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        } else {
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
    } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        if (queue_type_ == QueueType::Copy) {
            barrier.dstAccessMask = 0;
            destination_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        } else {
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
    } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destination_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;
        source_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destination_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        if (queue_type_ == QueueType::Copy) {
            barrier.dstAccessMask = 0;
            destination_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        } else {
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
    } else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destination_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destination_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::runtime_error("VulkanCommandBuffer::image_layout_barrier: unsupported layout transition");
    }

    vkCmdPipelineBarrier(
        vulkan_command_buffer_,
        source_stage,
        destination_stage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);
}

}// namespace ocarina



#include "vulkan_index_buffer.h"
#include "vulkan_device.h"
#include "vulkan_driver.h"
#include "vulkan_buffer.h"
#include "vulkan_command_buffer.h"
#include "rhi/fence.h"

namespace ocarina {

VulkanIndexBuffer::VulkanIndexBuffer(
    VulkanDevice* device,
    const void* initial_data,
    uint32_t indices_count,
    bool bit16)
    : device_(device) {
    bit16_ = bit16;
    const uint64_t size = indices_count * (bit16 ? sizeof(uint16_t) : sizeof(uint32_t));
    indices_.resize(indices_count);
    if (initial_data) {
        memcpy(indices_.data(), initial_data, size);
        load_from_cpu(initial_data, 0, static_cast<uint32_t>(size));
    }
}

VulkanIndexBuffer::~VulkanIndexBuffer() {
    if (vulkan_buffer_ != nullptr) {
        ocarina::delete_with_allocator<VulkanBuffer>(vulkan_buffer_);
    }
}

void VulkanIndexBuffer::load_from_cpu(const void* cpu_data, uint32_t byte_offset, uint32_t num_bytes) {
    (void)byte_offset;
    if (vulkan_buffer_ != nullptr) {
        ocarina::delete_with_allocator<VulkanBuffer>(vulkan_buffer_);
    }

    vulkan_buffer_ = device_->create_vulkan_buffer(
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        num_bytes,
        nullptr);

    VulkanBuffer staging_buffer(
        device_,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        num_bytes,
        cpu_data);

    CommandBuffer cmd = static_cast<VulkanDevice*>(device_)->get_command_buffer(QueueType::Copy);
    cmd.begin();
    VulkanCommandBuffer* vulkan_cmd = static_cast<VulkanCommandBuffer*>(cmd.impl());
    vulkan_cmd->copy_buffer(&staging_buffer, vulkan_buffer_);
    cmd.end();
    Fence fence = device_->create_fence();
    cmd.submit_to_queue(QueueType::Copy, &fence);
    fence.wait();
}

}// namespace ocarina

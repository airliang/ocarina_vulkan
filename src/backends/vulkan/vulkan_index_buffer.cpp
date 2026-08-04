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
    : IndexBuffer(device) {
    bit16_ = bit16;
    if (indices_count > 0 && initial_data != nullptr) {
        upload_indices(initial_data, indices_count);
    }
}

VulkanIndexBuffer::~VulkanIndexBuffer() {
    if (vulkan_buffer_ != nullptr) {
        ocarina::delete_with_allocator<VulkanBuffer>(vulkan_buffer_);
        vulkan_buffer_ = nullptr;
    }
}

void VulkanIndexBuffer::allocate_capacity(uint32_t max_indices) {
    if (max_indices == 0) {
        return;
    }

    VulkanDevice* device = static_cast<VulkanDevice*>(device_);
    const uint32_t stride = bit16_ ? sizeof(uint16_t) : sizeof(uint32_t);
    const uint64_t num_bytes = static_cast<uint64_t>(max_indices) * stride;

    if (vulkan_buffer_ != nullptr) {
        ocarina::delete_with_allocator<VulkanBuffer>(vulkan_buffer_);
        vulkan_buffer_ = nullptr;
    }

    vulkan_buffer_ = device->create_vulkan_buffer(
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        num_bytes,
        nullptr);
    capacity_indices_ = max_indices;
    indices_.clear();
    set_gpu_resource_state(GPUResourceState::GPU_Visible);
}

void VulkanIndexBuffer::upload_indices_range(
    const void* data,
    uint32_t index_offset,
    uint32_t index_count) {
    if (data == nullptr || index_count == 0) {
        return;
    }
    OC_ASSERT(vulkan_buffer_ != nullptr);
    OC_ASSERT(index_offset + index_count <= capacity_indices_);

    const uint32_t stride = bit16_ ? sizeof(uint16_t) : sizeof(uint32_t);
    const uint64_t num_bytes = static_cast<uint64_t>(index_count) * stride;
    const uint64_t dst_offset = static_cast<uint64_t>(index_offset) * stride;

    VulkanDevice* device = static_cast<VulkanDevice*>(device_);
    VulkanBuffer staging_buffer(
        device,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        num_bytes,
        data);

    CommandBuffer cmd = device->get_command_buffer(QueueType::Copy);
    cmd.begin();
    VulkanCommandBuffer* vulkan_cmd = static_cast<VulkanCommandBuffer*>(cmd.impl());
    vulkan_cmd->copy_buffer(&staging_buffer, vulkan_buffer_, 0, dst_offset, num_bytes);
    cmd.end();
    Fence fence = device_->create_fence();
    cmd.submit_to_queue(QueueType::Copy, &fence);
    fence.wait();
    device->release_command_buffer(cmd);
}

void VulkanIndexBuffer::upload_indices(const void* data, uint32_t indices_count) {
    if (data == nullptr || indices_count == 0) {
        return;
    }

    const uint32_t stride = bit16_ ? sizeof(uint16_t) : sizeof(uint32_t);
    const uint32_t num_bytes = indices_count * stride;
    indices_.resize(indices_count);
    memcpy(indices_.data(), data, num_bytes);
    load_from_cpu(data, 0, num_bytes);
    set_gpu_resource_state(GPUResourceState::GPU_Ready);
}

void VulkanIndexBuffer::load_from_cpu(const void* cpu_data, uint32_t byte_offset, uint32_t num_bytes) {
    (void)byte_offset;
    if (num_bytes == 0 || cpu_data == nullptr) {
        return;
    }

    VulkanDevice* device = static_cast<VulkanDevice*>(device_);
    if (vulkan_buffer_ != nullptr) {
        ocarina::delete_with_allocator<VulkanBuffer>(vulkan_buffer_);
        vulkan_buffer_ = nullptr;
        capacity_indices_ = 0;
    }

    vulkan_buffer_ = device->create_vulkan_buffer(
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        num_bytes,
        nullptr);

    VulkanBuffer staging_buffer(
        device,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        num_bytes,
        cpu_data);

    CommandBuffer cmd = device->get_command_buffer(QueueType::Copy);
    cmd.begin();
    VulkanCommandBuffer* vulkan_cmd = static_cast<VulkanCommandBuffer*>(cmd.impl());
    vulkan_cmd->copy_buffer(&staging_buffer, vulkan_buffer_);
    cmd.end();
    Fence fence = device_->create_fence();
    cmd.submit_to_queue(QueueType::Copy, &fence);
    fence.wait();
    device->release_command_buffer(cmd);

    const uint32_t stride = bit16_ ? sizeof(uint16_t) : sizeof(uint32_t);
    capacity_indices_ = num_bytes / stride;
}

}// namespace ocarina

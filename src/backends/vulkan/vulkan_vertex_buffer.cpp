#include "vulkan_vertex_buffer.h"
#include "vulkan_shader.h"
#include "vulkan_device.h"
#include "vulkan_buffer.h"
#include "vulkan_driver.h"
#include "vulkan_command_buffer.h"
#include "rhi/fence.h"

namespace ocarina {

VulkanVertexBuffer::VulkanVertexBuffer(VulkanDevice* device) : VertexBuffer(device) {}

VulkanVertexBuffer::~VulkanVertexBuffer() {
    for (size_t i = 0; i < (size_t)VertexAttributeType::Enum::Count; ++i) {
        if (vertex_streams_[(uint8_t)i].data) {
            delete[] vertex_streams_[(uint8_t)i].data;
            vertex_streams_[(uint8_t)i].data = nullptr;
        }
        if (vertex_streams_[(uint8_t)i].buffer != 0) {
            device_->destroy_buffer(vertex_streams_[(uint8_t)i].buffer);
            vertex_streams_[(uint8_t)i].buffer = 0;
        }
    }
}

void VulkanVertexBuffer::allocate_stream_capacity(
    VertexAttributeType::Enum type,
    uint32_t capacity,
    uint32_t stride) {
    if (capacity == 0 || stride == 0) {
        return;
    }

    VertexStream* stream = get_vertex_stream(type);
    if (stream == nullptr) {
        return;
    }

    stream->type = type;
    stream->count = capacity;
    stream->stride = stride;
    stream->offset = 0;
    if (type == VertexAttributeType::Enum::Position) {
        vertex_count_ = capacity;
    }

    const uint64_t byte_size = static_cast<uint64_t>(capacity) * stride;
    if (stream->buffer != 0) {
        device_->destroy_buffer(stream->buffer);
        stream->buffer = 0;
    }

    VulkanDevice* device = static_cast<VulkanDevice*>(device_);
    VulkanBuffer* buffer = device->create_vulkan_buffer(
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        byte_size,
        nullptr);
    stream->buffer = (handle_ty)buffer;
    set_gpu_resource_state(GPUResourceState::GPU_Visible);
}

void VulkanVertexBuffer::upload_attribute_range(
    VertexAttributeType::Enum type,
    const void* data,
    uint32_t vertex_offset,
    uint32_t vertex_count) {
    if (data == nullptr || vertex_count == 0) {
        return;
    }

    VertexStream* stream = get_vertex_stream(type);
    if (stream == nullptr || stream->buffer == 0 || stream->stride == 0) {
        return;
    }
    OC_ASSERT(vertex_offset + vertex_count <= stream->count);

    const uint64_t byte_size = static_cast<uint64_t>(vertex_count) * stream->stride;
    const uint64_t dst_offset = static_cast<uint64_t>(vertex_offset) * stream->stride;

    VulkanDevice* device = static_cast<VulkanDevice*>(device_);
    VulkanBuffer staging_buffer(
        device,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        byte_size,
        data);

    CommandBuffer cmd = device->get_command_buffer(QueueType::Copy);
    cmd.begin();
    VulkanCommandBuffer* vulkan_cmd = static_cast<VulkanCommandBuffer*>(cmd.impl());
    vulkan_cmd->copy_buffer(
        &staging_buffer,
        (VulkanBuffer*)stream->buffer,
        0,
        dst_offset,
        byte_size);
    cmd.end();
    Fence fence = device_->create_fence();
    cmd.submit_to_queue(QueueType::Copy, &fence);
    fence.wait();
    device->release_command_buffer(cmd);
}

void VulkanVertexBuffer::upload_attribute_data(VertexAttributeType::Enum type, const void* data, uint64_t offset) {
    (void)data;
    VertexStream* stream = get_vertex_stream(type);
    if (stream == nullptr || stream->data == nullptr) {
        return;
    }

    VulkanDevice* device = static_cast<VulkanDevice*>(device_);
    if (stream->buffer == 0) {
        VulkanBuffer* buffer = device->create_vulkan_buffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            stream->get_size(),
            nullptr);
        stream->buffer = (handle_ty)buffer;
    }

    VulkanBuffer staging_buffer(
        device,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stream->get_size(),
        stream->data);

    CommandBuffer cmd = device->get_command_buffer(QueueType::Copy);
    cmd.begin();
    VulkanCommandBuffer* vulkan_cmd = static_cast<VulkanCommandBuffer*>(cmd.impl());
    vulkan_cmd->copy_buffer(
        &staging_buffer,
        (VulkanBuffer*)stream->buffer,
        0,
        offset,
        stream->get_size());
    cmd.end();
    Fence fence = device_->create_fence();
    cmd.submit_to_queue(QueueType::Copy, &fence);
    fence.wait();
    device->release_command_buffer(cmd);
}

}// namespace ocarina

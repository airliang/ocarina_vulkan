#pragma once
#include "vulkan_vertex_buffer.h"
#include "vulkan_shader.h"
#include "vulkan_device.h"
#include "vulkan_buffer.h"
#include "vulkan_driver.h"
#include "vulkan_command_buffer.h"
#include "rhi/fence.h"

namespace ocarina {

VulkanVertexBuffer::VulkanVertexBuffer(VulkanDevice *device) : VertexBuffer(device) {

}

VulkanVertexBuffer::~VulkanVertexBuffer()
{
    //for (auto it : vertex_bindings_)
    //{
    //    ocarina::delete_with_allocator(it.second);
    //}

    for (size_t i = 0; i < (size_t)VertexAttributeType::Enum::Count; ++i)
    {
        if (vertex_streams_[(uint8_t)i].data)
        {
            delete[] vertex_streams_[(uint8_t)i].data;
            vertex_streams_[(uint8_t)i].data = nullptr;
        }
        if (vertex_streams_[(uint8_t)i].buffer != 0)
        {
            device_->destroy_buffer(vertex_streams_[(uint8_t)i].buffer);
            vertex_streams_[(uint8_t)i].buffer = 0;
        }
    }
}

/*
VulkanVertexStreamBinding *VulkanVertexBuffer::get_or_create_vertex_binding(VulkanShader *vertex_shader) {
    auto it = vertex_bindings_.find((handle_ty)vertex_shader->shader_module());

    if (it != vertex_bindings_.end())
    {
        return it->second;
    }

    auto binding = ocarina::new_with_allocator<VulkanVertexStreamBinding>();
    VulkanVertexStreamBinding::create_from_vertex_shader(vertex_shader, this, *binding);
    vertex_bindings_.insert(std::make_pair((handle_ty)vertex_shader->shader_module(), binding));

    return binding;
}
*/

void VulkanVertexBuffer::upload_attribute_data(VertexAttributeType::Enum type, const void* data, uint64_t offset) {
    VertexStream* stream = get_vertex_stream(type);
    if (stream == nullptr || stream->data == nullptr) {
        return; // No data to upload
    }

    VulkanDevice *device = static_cast<VulkanDevice *>(device_);
    if (stream->buffer == 0) {
        // Create a new buffer if it doesn't exist
        VulkanBuffer *buffer = device->create_vulkan_buffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            stream->get_size(),
            nullptr);
        stream->buffer = (handle_ty)buffer;
    }
    
    VulkanBuffer staging_buffer(device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stream->get_size(), stream->data);

    
    //VulkanDriver::instance().copy_buffer(&staging_buffer, (VulkanBuffer*)stream->buffer);
    CommandBuffer cmd = static_cast<VulkanDevice*>(device_)->get_command_buffer(QueueType::Copy);
    cmd.begin();
    VulkanCommandBuffer* vulkan_cmd = static_cast<VulkanCommandBuffer*>(cmd.impl());
    vulkan_cmd->copy_buffer(&staging_buffer, (VulkanBuffer*)stream->buffer);
    cmd.end();
    Fence fence = device_->create_fence();
    cmd.submit_to_queue(QueueType::Copy, &fence);
    fence.wait();
}

}// namespace ocarina
#pragma once

#include "rhi/vertex_buffer.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanDevice;

class VulkanVertexBuffer : public VertexBuffer {
public:
    VulkanVertexBuffer(VulkanDevice* device);
    ~VulkanVertexBuffer();

    void allocate_stream_capacity(VertexAttributeType::Enum type, uint32_t capacity, uint32_t stride) override;
    void upload_attribute_range(
        VertexAttributeType::Enum type,
        const void* data,
        uint32_t vertex_offset,
        uint32_t vertex_count) override;
    void upload_attribute_data(VertexAttributeType::Enum type, const void* data, uint64_t offset = 0) override;
};

}// namespace ocarina

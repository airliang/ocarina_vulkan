#pragma once

#include "rhi/vertex_buffer.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanDevice;

class VulkanVertexBuffer : public VertexBuffer {
public:
    VulkanVertexBuffer(VulkanDevice* device);
    ~VulkanVertexBuffer();

    void upload_attribute_data(VertexAttributeType::Enum type, const void* data, uint64_t offset = 0) override;
};

}// namespace ocarina

#pragma once
#include "rhi/vertex_buffer.h"
#include <vulkan/vulkan.h>
namespace ocarina {
class VulkanDevice;
class VulkanShader;
class VulkanVertexBuffer;

class VulkanVertexBuffer : public VertexBuffer {
public:
    VulkanVertexBuffer(VulkanDevice* device);
    ~VulkanVertexBuffer();
    
    //VulkanVertexStreamBinding *get_or_create_vertex_binding(VulkanShader *vertex_shader);

    void upload_attribute_data(VertexAttributeType::Enum type, const void* data, uint64_t offset = 0) override;

private:
    //std::unordered_map<handle_ty, VulkanVertexStreamBinding*> vertex_bindings_;
    //std::unordered_map<VertexAttributeType::Enum, VertexStream*> vertex_streams_;
};

}// namespace ocarina
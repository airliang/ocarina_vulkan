#pragma once

#include "core/header.h"
#include "core/concepts.h"
#include "core/stl.h"
#include "core/singleton.h"
#include "rhi/index_buffer.h"
#include <vulkan/vulkan.h>
#include "vulkan_buffer.h"

namespace ocarina {

class VulkanDevice;

class VulkanIndexBuffer : public IndexBuffer {
public:
    VulkanIndexBuffer(VulkanDevice* device, const void* initial_data, uint32_t indices_count, bool bit16);
    ~VulkanIndexBuffer() override;

    void upload_indices(const void* data, uint32_t indices_count) override;
    void allocate_capacity(uint32_t max_indices) override;
    void upload_indices_range(const void* data, uint32_t index_offset, uint32_t index_count) override;
    void load_from_cpu(const void* cpu_data, uint32_t byte_offset, uint32_t num_bytes);

    VkBuffer buffer_handle() const {
        return vulkan_buffer_ != nullptr ? vulkan_buffer_->buffer_handle() : VK_NULL_HANDLE;
    }

private:
    VulkanBuffer* vulkan_buffer_ = nullptr;
    uint32_t capacity_indices_ = 0;
};

}// namespace ocarina

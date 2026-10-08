#include "util.h"
#include "vulkan_device.h"
#include "vulkan_buffer.h"
#include "vulkan_driver.h"
#include "vulkan_vma.h"

#include <cstring>

namespace ocarina {

VulkanBuffer::VulkanBuffer(VulkanDevice *device, VkBufferUsageFlags usage_flags, VkMemoryPropertyFlags memory_property_flags, 
    VkDeviceSize size, const void *data ) : Buffer(device, 0, static_cast<size_t>(size)), device_(device), usage_(usage_flags) {
    memory_property_flags_ = memory_property_flags;

    VmaAllocator allocator = VulkanDriver::instance().allocator();
    OC_ASSERT(allocator != VK_NULL_HANDLE);

    VkBufferCreateInfo buffer_create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_create.usage = usage_flags;
    buffer_create.size = size;
    size_in_byte_ = static_cast<size_t>(size);

    VmaAllocationCreateInfo alloc_create = make_vma_allocation_info(memory_property_flags_);
    OC_ASSERT((usage_ & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) == 0);

    VK_CHECK_RESULT(vmaCreateBuffer(
        allocator,
        &buffer_create,
        &alloc_create,
        &vulkan_buffer_,
        &allocation_,
        &allocation_info_));
    memory_allocation_size_ = allocation_info_.size;

    if (data) {
        load_from_cpu(data, 0, size);
    }
}

VulkanBuffer::~VulkanBuffer() {
    unmap();
    VmaAllocator allocator = VulkanDriver::instance().allocator();
    if (allocator != VK_NULL_HANDLE && (vulkan_buffer_ != VK_NULL_HANDLE || allocation_ != VK_NULL_HANDLE)) {
        vmaDestroyBuffer(allocator, vulkan_buffer_, allocation_);
        vulkan_buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
    }
}

void VulkanBuffer::load_from_cpu(const void *cpu_data, VkDeviceSize byte_offset,
                                 VkDeviceSize size) {
    if (cpu_data == nullptr || allocation_ == VK_NULL_HANDLE) {
        return;
    }

    VmaAllocator allocator = VulkanDriver::instance().allocator();
    void *mapped = allocation_info_.pMappedData;
    bool need_unmap = false;
    if (mapped == nullptr) {
        VK_CHECK_RESULT(vmaMapMemory(allocator, allocation_, &mapped));
        need_unmap = true;
    }

    std::memcpy(static_cast<std::byte *>(mapped) + byte_offset, cpu_data, static_cast<size_t>(size));

    if ((memory_property_flags_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        VkDeviceSize flush_size = size;
        if ((memory_property_flags_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
            const VkDeviceSize atom_size = device_->device_limits().nonCoherentAtomSize;
            VkDeviceSize range_end = byte_offset + size;
            if (range_end < memory_allocation_size_) {
                range_end = ((range_end + atom_size - 1) / atom_size) * atom_size;
            } else {
                range_end = memory_allocation_size_;
            }
            flush_size = range_end - byte_offset;
        }
        flush(flush_size, byte_offset);
    }

    if (need_unmap) {
        vmaUnmapMemory(allocator, allocation_);
    }
}

VkResult VulkanBuffer::bind(VkDeviceSize offset)
{
    (void)offset;
    // vmaCreateBuffer already binds memory at allocation offset 0.
    return VK_SUCCESS;
}

VkResult VulkanBuffer::flush(VkDeviceSize size, VkDeviceSize offset) {
    VmaAllocator allocator = VulkanDriver::instance().allocator();
    if (allocator == VK_NULL_HANDLE || allocation_ == VK_NULL_HANDLE) {
        return VK_ERROR_UNKNOWN;
    }
    if (size == VK_WHOLE_SIZE) {
        size = memory_allocation_size_ - offset;
    }
    return vmaFlushAllocation(allocator, allocation_, offset, size);
}

void VulkanBuffer::map() noexcept {
    if (mapped_ != nullptr) {
        return;
    }
    if (allocation_info_.pMappedData != nullptr) {
        mapped_ = allocation_info_.pMappedData;
        return;
    }
    VmaAllocator allocator = VulkanDriver::instance().allocator();
    VK_CHECK_RESULT(vmaMapMemory(allocator, allocation_, &mapped_));
}

void VulkanBuffer::unmap() noexcept {
    if (mapped_ == nullptr) {
        return;
    }
    // Persistently mapped allocations must not be unmapped via vmaUnmapMemory.
    if (allocation_info_.pMappedData != nullptr) {
        mapped_ = nullptr;
        return;
    }
    VmaAllocator allocator = VulkanDriver::instance().allocator();
    if (allocator != VK_NULL_HANDLE && allocation_ != VK_NULL_HANDLE) {
        vmaUnmapMemory(allocator, allocation_);
    }
    mapped_ = nullptr;
}

}// namespace ocarina

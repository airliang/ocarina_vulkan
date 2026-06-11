#include "util.h"
#include "vulkan_device.h"
#include "vulkan_buffer.h"

namespace ocarina {

VulkanBuffer::VulkanBuffer(VulkanDevice *device, VkBufferUsageFlags usage_flags, VkMemoryPropertyFlags memory_property_flags, 
    VkDeviceSize size, const void *data ) : device_(device) {
    memory_property_flags_ = memory_property_flags;
    VkBufferCreateInfo buffer_create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_create.usage = usage_flags;
    buffer_create.size = size;
    VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice(), &buffer_create, nullptr, &vulkan_buffer_));
    size_ = size;

    // Create the memory backing up the buffer handle
    VkMemoryRequirements memReqs;
    VkMemoryAllocateInfo memAlloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};

    vkGetBufferMemoryRequirements(device_->logicalDevice(), vulkan_buffer_, &memReqs);
    memAlloc.allocationSize = memReqs.size;
    memory_allocation_size_ = memReqs.size;
    // Find a memory type index that fits the properties of the buffer
    memAlloc.memoryTypeIndex = device_->get_memory_type(memReqs.memoryTypeBits, memory_property_flags_);
    // If the buffer has VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT set we also need to enable the appropriate flag during allocation
    VkMemoryAllocateFlagsInfoKHR allocFlagsInfo{};
    if (usage_ & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
        memAlloc.pNext = &allocFlagsInfo;
    }
    VK_CHECK_RESULT(vkAllocateMemory(device_->logicalDevice(), &memAlloc, nullptr, &memory_));

    VK_CHECK_RESULT(vkBindBufferMemory(device_->logicalDevice(), vulkan_buffer_, memory_, 0)); 
    if (data)
    {
        load_from_cpu(data, 0, size);
    }
}

VulkanBuffer::~VulkanBuffer() {
    unmap();
    if (vulkan_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_->logicalDevice(), vulkan_buffer_, nullptr);
    }

    if (memory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(device_->logicalDevice(), memory_, nullptr);
    }
}

void VulkanBuffer::load_from_cpu(const void *cpu_data, VkDeviceSize byte_offset,
                                 VkDeviceSize size) {
    // If a pointer to the buffer data has been passed, map the buffer and copy over the data
    if (cpu_data != nullptr) {
        VkDeviceSize map_size = size;
        VkDeviceSize flush_size = size;
        if ((memory_property_flags_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
            const VkDeviceSize atom_size = device_->device_limits().nonCoherentAtomSize;
            VkDeviceSize range_end = byte_offset + size;
            if (range_end < memory_allocation_size_) {
                range_end = ((range_end + atom_size - 1) / atom_size) * atom_size;
            } else {
                range_end = memory_allocation_size_;
            }
            map_size = range_end - byte_offset;
            flush_size = map_size;
        }

        VK_CHECK_RESULT(vkMapMemory(device_->logicalDevice(), memory_, byte_offset, map_size, 0, &mapped_));
        memcpy(mapped_, cpu_data, size);
        if ((memory_property_flags_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
            flush(flush_size, byte_offset);

        if (mapped_) {
            vkUnmapMemory(device_->logicalDevice(), memory_);
            mapped_ = nullptr;
        }
    }
}

VkResult VulkanBuffer::bind(VkDeviceSize offset)
{
    return vkBindBufferMemory(device_->logicalDevice(), vulkan_buffer_, memory_, offset);
}

VkResult VulkanBuffer::flush(VkDeviceSize size, VkDeviceSize offset) {
    if (mapped_ == nullptr) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (size == VK_WHOLE_SIZE) {
        size = memory_allocation_size_ - offset;
    }

    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = memory_;
    range.offset = offset;
    range.size = size;

    return vkFlushMappedMemoryRanges(device_->logicalDevice(), 1, &range);
}

void VulkanBuffer::map() noexcept {
    if (mapped_ == nullptr) {
        VK_CHECK_RESULT(vkMapMemory(device_->logicalDevice(), memory_, 0, memory_allocation_size_, 0, &mapped_));
    }
}

void VulkanBuffer::unmap() noexcept {
    if (mapped_) {
        vkUnmapMemory(device_->logicalDevice(), memory_);
        mapped_ = nullptr;
    }
}

}// namespace ocarina



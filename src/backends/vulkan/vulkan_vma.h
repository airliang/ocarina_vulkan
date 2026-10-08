#pragma once

#include "rhi/graphics_descriptions.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace ocarina {

/// Map engine memory usage to VMA create info (VMA 3.x AUTO + host-access flags).
[[nodiscard]] inline VmaAllocationCreateInfo make_vma_allocation_info(DeviceMemoryUsage usage) {
    VmaAllocationCreateInfo info{};
    info.usage = VMA_MEMORY_USAGE_AUTO;
    switch (usage) {
        case DeviceMemoryUsage::MEMORY_USAGE_GPU_ONLY:
            info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;
        case DeviceMemoryUsage::MEMORY_USAGE_CPU_ONLY:
            info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case DeviceMemoryUsage::MEMORY_USAGE_CPU_TO_GPU:
            info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT
                | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case DeviceMemoryUsage::MEMORY_USAGE_GPU_TO_CPU:
            info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case DeviceMemoryUsage::MEMORY_USAGE_UNKNOWN:
        default:
            break;
    }
    return info;
}

/// Bridge existing call sites that still pass VkMemoryPropertyFlags.
[[nodiscard]] inline VmaAllocationCreateInfo make_vma_allocation_info(
    VkMemoryPropertyFlags memory_property_flags) {
    VmaAllocationCreateInfo info{};
    info.usage = VMA_MEMORY_USAGE_AUTO;
    info.requiredFlags = memory_property_flags;
    if ((memory_property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
        info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
            | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    return info;
}

}// namespace ocarina

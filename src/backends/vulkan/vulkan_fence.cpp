#pragma once
#include "vulkan_fence.h"
#include "vulkan_device.h"
#include "vulkan_driver.h"
#include <vulkan/vulkan.h>
namespace ocarina {

VulkanFence::VulkanFence(VulkanDevice* device) {
    device_ = device;
    VkFenceCreateInfo fence_create_info = {};
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_create_info.flags = 0; // Create the fence in the unsignaled state
    if (vkCreateFence(device_->logicalDevice(), &fence_create_info, nullptr, &vulkan_fence_) != VK_SUCCESS) {
        OC_ERROR("Failed to create Vulkan fence");
    }
}

VulkanFence::~VulkanFence() {
    if (vulkan_fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(device_->logicalDevice(), vulkan_fence_, nullptr);
    }
}

void VulkanFence::reset() {
    if (vulkan_fence_ != VK_NULL_HANDLE) {
        vkResetFences(device_->logicalDevice(), 1, &vulkan_fence_);
    }
}

bool VulkanFence::is_finished() const {
    if (vulkan_fence_ != VK_NULL_HANDLE) {
        VkResult result = vkGetFenceStatus(device_->logicalDevice(), vulkan_fence_);
        return result == VK_SUCCESS; // VK_SUCCESS means the fence is signaled
    }
    return true; // If the fence handle is null, we consider it as finished
}



}// namespace ocarina

OC_EXPORT_API ocarina::Fence::Impl* create_fence() {
    return ocarina::new_with_allocator<ocarina::VulkanFence>(ocarina::VulkanDriver::instance().get_device());
}
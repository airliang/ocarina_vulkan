#include "vulkan_semaphore.h"
#include "vulkan_device.h"
#include "util.h"

#include <limits>

namespace ocarina {

VulkanSemaphore::VulkanSemaphore(VulkanDevice *device, uint64_t initial_value)
    : device_(device), is_timeline_(true), owns_handle_(true) {
    OC_ASSERT(device_ != nullptr);

    VkSemaphoreTypeCreateInfo timeline_info{};
    timeline_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timeline_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timeline_info.initialValue = initial_value;

    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    sem_info.pNext = &timeline_info;

    VK_CHECK_RESULT(vkCreateSemaphore(device_->logicalDevice(), &sem_info, nullptr, &semaphore_));
}

VulkanSemaphore::VulkanSemaphore(
    VulkanDevice *device,
    VkSemaphore semaphore,
    bool is_timeline,
    bool owns_handle)
    : device_(device)
    , semaphore_(semaphore)
    , is_timeline_(is_timeline)
    , owns_handle_(owns_handle) {
    OC_ASSERT(device_ != nullptr);
    OC_ASSERT(semaphore_ != VK_NULL_HANDLE);
}

VulkanSemaphore::~VulkanSemaphore() {
    if (owns_handle_ && semaphore_ != VK_NULL_HANDLE && device_ != nullptr) {
        vkDestroySemaphore(device_->logicalDevice(), semaphore_, nullptr);
    }
    semaphore_ = VK_NULL_HANDLE;
}

uint64_t VulkanSemaphore::get_counter_value() const noexcept {
    if (!is_timeline_ || semaphore_ == VK_NULL_HANDLE || device_ == nullptr) {
        return 0;
    }
    uint64_t value = 0;
    VK_CHECK_RESULT(vkGetSemaphoreCounterValue(device_->logicalDevice(), semaphore_, &value));
    return value;
}

void VulkanSemaphore::wait(uint64_t value, uint64_t timeout_ns) const {
    if (!is_timeline_ || semaphore_ == VK_NULL_HANDLE || device_ == nullptr || value == 0) {
        return;
    }
    if (get_counter_value() >= value) {
        return;
    }

    VkSemaphoreWaitInfo wait_info{};
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wait_info.semaphoreCount = 1;
    wait_info.pSemaphores = &semaphore_;
    wait_info.pValues = &value;
    VK_CHECK_RESULT(vkWaitSemaphores(device_->logicalDevice(), &wait_info, timeout_ns));
}

}// namespace ocarina

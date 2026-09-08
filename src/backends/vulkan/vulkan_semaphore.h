#pragma once

#include "rhi/semaphore.h"
#include <limits>
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanDevice;

class VulkanSemaphore : public Semaphore::Impl {
public:
    /// Create an owned timeline semaphore.
    explicit VulkanSemaphore(VulkanDevice *device, uint64_t initial_value = 0);

    /// Wrap an existing VkSemaphore (optionally taking ownership).
    VulkanSemaphore(
        VulkanDevice *device,
        VkSemaphore semaphore,
        bool is_timeline,
        bool owns_handle);

    ~VulkanSemaphore() override;

    [[nodiscard]] handle_ty native_handle() const noexcept override {
        return reinterpret_cast<handle_ty>(semaphore_);
    }

    [[nodiscard]] bool is_timeline() const noexcept override { return is_timeline_; }

    [[nodiscard]] uint64_t get_counter_value() const noexcept override;

    void wait(
        uint64_t value,
        uint64_t timeout_ns = std::numeric_limits<uint64_t>::max()) const override;

    [[nodiscard]] VkSemaphore vk_handle() const noexcept { return semaphore_; }

private:
    VulkanDevice *device_ = nullptr;
    VkSemaphore semaphore_ = VK_NULL_HANDLE;
    bool is_timeline_ = false;
    bool owns_handle_ = false;
};

}// namespace ocarina

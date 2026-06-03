#pragma once
#include "rhi/fence.h"
#include <vulkan/vulkan.h>
namespace ocarina {
class VulkanDevice;


class VulkanFence : public Fence::Impl {
public:
    VulkanFence(VulkanDevice *device);
    ~VulkanFence();
    void reset() override;
    bool is_finished() const override;

    VkFence fence_handle() const
    {
        return vulkan_fence_;
    }

    handle_ty native_handle() const override
    {
        return reinterpret_cast<handle_ty>(vulkan_fence_);
    }

    void wait(uint64_t timeout = std::numeric_limits<uint64_t>::max()) const override;
private:
    VkFence vulkan_fence_ = VK_NULL_HANDLE;
    VulkanDevice* device_ = nullptr;
};

}// namespace ocarina
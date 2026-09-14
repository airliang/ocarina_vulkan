#pragma once

#include "core/stl.h"
#include "rhi/resources/cubemap.h"
#include "rhi/resources/texture_sampler.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanDevice;

class VulkanCubemap : public Cubemap::Impl {
public:
    VulkanCubemap(
        VulkanDevice *device,
        uint32_t width,
        uint32_t height,
        PixelStorage pixel_storage,
        const TextureSampler &sampler);
    ~VulkanCubemap() override;

    [[nodiscard]] uint2 face_resolution() const noexcept override { return face_res_; }
    [[nodiscard]] PixelStorage pixel_storage() const noexcept override { return pixel_storage_; }
    [[nodiscard]] uint32_t mip_levels() const noexcept override { return mip_levels_; }
    [[nodiscard]] handle_ty image_handle() const noexcept override {
        return reinterpret_cast<handle_ty>(image_);
    }
    [[nodiscard]] const TextureSampler *get_sampler_pointer() const noexcept override {
        return &texture_sampler_;
    }
    [[nodiscard]] const void *handle_ptr() const noexcept override { return &image_; }

    [[nodiscard]] VkImage vk_image() const noexcept { return image_; }
    [[nodiscard]] VkImageView vk_image_view() const noexcept { return image_view_; }
    [[nodiscard]] VkImageLayout vk_image_layout() const noexcept { return image_layout_; }
    [[nodiscard]] VkImageAspectFlags vk_aspect_mask() const noexcept { return VK_IMAGE_ASPECT_COLOR_BIT; }
    [[nodiscard]] uint32_t layer_count() const noexcept { return 6; }

    void set_image_layout(VkImageLayout layout) noexcept { image_layout_ = layout; }

    [[nodiscard]] VkDescriptorImageInfo get_descriptor_info() const {
        VkDescriptorImageInfo info{};
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        info.imageView = image_view_;
        info.sampler = sampler_;
        return info;
    }

    [[nodiscard]] VkDescriptorImageInfo get_sampled_image_descriptor_info() const {
        VkDescriptorImageInfo info{};
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        info.imageView = image_view_;
        info.sampler = VK_NULL_HANDLE;
        return info;
    }

private:
    void create_sampler(const TextureSampler &sampler_creation);

    VulkanDevice *device_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkDeviceMemory image_memory_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkImageLayout image_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFormat image_format_ = VK_FORMAT_UNDEFINED;
    PixelStorage pixel_storage_{};
    TextureSampler texture_sampler_{};
    uint2 face_res_{};
    uint32_t mip_levels_ = 1;
};

}// namespace ocarina

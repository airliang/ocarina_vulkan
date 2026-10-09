#pragma once

#include "core/stl.h"
#include "rhi/resources/cubemap.h"
#include "rhi/resources/texture_sampler.h"
#include <vk_mem_alloc.h>
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
        const TextureSampler &sampler,
        TextureUsageFlags usage,
        uint32_t mip_levels = 1);
    ~VulkanCubemap() override;

    [[nodiscard]] uint2 face_resolution() const noexcept override { return face_res_; }
    [[nodiscard]] PixelStorage pixel_storage() const noexcept override { return pixel_storage_; }
    [[nodiscard]] uint32_t mip_levels() const noexcept override { return mip_levels_; }
    [[nodiscard]] TextureUsageFlags usage_flags() const noexcept override { return usage_flags_; }
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

    [[nodiscard]] VkDescriptorImageInfo get_storage_image_descriptor_info(uint32_t mip_level = 0) const {
        VkDescriptorImageInfo info{};
        info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkImageView view = image_view_;
        if (!storage_mip_views_.empty()) {
            const uint32_t mip = mip_level < storage_mip_views_.size()
                                     ? mip_level
                                     : static_cast<uint32_t>(storage_mip_views_.size() - 1);
            view = storage_mip_views_[mip];
        }
        info.imageView = view;
        info.sampler = VK_NULL_HANDLE;
        return info;
    }

private:
    void create_sampler(const TextureSampler &sampler_creation);
    void create_storage_mip_views();

    VulkanDevice *device_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    ocarina_vector<VkImageView> storage_mip_views_;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkImageLayout image_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFormat image_format_ = VK_FORMAT_UNDEFINED;
    PixelStorage pixel_storage_{};
    TextureSampler texture_sampler_{};
    TextureUsageFlags usage_flags_ = TextureUsageFlags::None;
    uint2 face_res_{};
    uint32_t mip_levels_ = 1;
};

}// namespace ocarina

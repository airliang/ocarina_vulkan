//
// Created by Zero on 06/08/2022.
//

#pragma once

#include "core/stl.h"
#include "rhi/resources/texture.h"
#include "rhi/resources/texture_sampler.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanDevice;
class Image;

class VulkanTexture : public Texture::Impl {
private:
    VkImageType imageType_;
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkImageLayout image_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkSampler sampler_ = VK_NULL_HANDLE;
    PixelStorage pixel_storage_;
    uint3 res_{};
    VulkanDevice* device_ = nullptr;
    VkFormat image_format_ = VK_FORMAT_UNDEFINED;
    uint32_t mip_levels_ = 1;
    VkDeviceMemory image_memory_ = VK_NULL_HANDLE;
    TextureSampler texture_sampler_;
    TextureUsageFlags usage_flags_ = TextureUsageFlags::None;
    VkImageAspectFlags aspect_mask_ = VK_IMAGE_ASPECT_COLOR_BIT;
    bool is_render_target_ = false;
public:
    VulkanTexture(VulkanDevice *device, Image *image, const TextureViewCreation& texture_view, const TextureSampler& sampler);
    VulkanTexture(VulkanDevice *device, uint32_t width, uint32_t height, uint32_t depth, PixelStorage format, const TextureViewCreation &texture_view,
        const TextureSampler& sampler, uint4 default_color, const void* data);
    VulkanTexture(VulkanDevice* device, uint32_t width, uint32_t height, PixelStorage format, TextureUsageFlags usage);
    ~VulkanTexture() override;
    void init(Image *image, const TextureViewCreation &texture_view);
    void init_from_pixels(uint32_t width, uint32_t height, uint32_t depth, PixelStorage format, const TextureViewCreation &texture_view,
        const TextureSampler& sampler, uint4 default_color, const void* data);
    void init_render_target(uint32_t width, uint32_t height, PixelStorage format, TextureUsageFlags usage);
    void create_image_view(const TextureViewCreation &texture_view);
    void create_render_target_image_view();
    void create_sampler(const TextureSampler& sampler_creation);
    void load_cpu_data(Image *image);
    void load_cpu_data(const void* data, size_t size_in_bytes);
    [[nodiscard]] uint3 resolution() const noexcept override { return res_; }
    [[nodiscard]] handle_ty array_handle() const noexcept override {
        return reinterpret_cast<handle_ty>(image_);
    }
    [[nodiscard]] const handle_ty *array_handle_ptr() const noexcept override {
        return reinterpret_cast<handle_ty *>(image_);
    }
    [[nodiscard]] handle_ty tex_handle() const noexcept override {
        return reinterpret_cast<handle_ty>(image_);
    }
    [[nodiscard]] const void *handle_ptr() const noexcept override {
        return &image_;
    }
    [[nodiscard]] size_t data_size() const noexcept override;
    [[nodiscard]] size_t data_alignment() const noexcept override;
    [[nodiscard]] size_t max_member_size() const noexcept override;
    [[nodiscard]] PixelStorage pixel_storage() const noexcept override { return pixel_storage_; }
    [[nodiscard]] const TextureSampler* get_sampler_pointer() const noexcept { return &texture_sampler_; }
    [[nodiscard]] bool is_render_target() const noexcept override { return is_render_target_; }
    [[nodiscard]] TextureUsageFlags usage_flags() const noexcept override { return usage_flags_; }
    uint32_t mip_levels() const noexcept { return mip_levels_; }
    uint32_t width() const noexcept { return res_.x; }
    uint32_t height() const noexcept { return res_.y; }
    uint32_t depth() const noexcept { return res_.z; }
    VkFormat vk_format() const noexcept { return image_format_; }
    VkImageView vk_image_view() const noexcept { return image_view_; }
    VkImage vk_image() const noexcept { return image_; }
    VkImageLayout vk_image_layout() const noexcept { return image_layout_; }
    VkImageAspectFlags vk_aspect_mask() const noexcept { return aspect_mask_; }
    bool is_depth_stencil() const noexcept {
        return (static_cast<uint32_t>(usage_flags_) & static_cast<uint32_t>(TextureUsageFlags::DepthStencil)) != 0;
    }

    void set_image_layout(VkImageLayout new_image_layout)
    {
        image_layout_ = new_image_layout;
    }

    VkDescriptorImageInfo get_descriptor_info() const
    {
        VkDescriptorImageInfo image_info{};
        image_info.imageLayout = image_layout_;
        image_info.imageView = image_view_;
        image_info.sampler = sampler_;
        return image_info;
    }

    VkDescriptorImageInfo get_sampled_image_descriptor_info() const
    {
        VkDescriptorImageInfo image_info{};
        image_info.imageLayout = image_layout_;
        image_info.imageView = image_view_;
        image_info.sampler = VK_NULL_HANDLE;
        return image_info;
    }
};
}// namespace ocarina
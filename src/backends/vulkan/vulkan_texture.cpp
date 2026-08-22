//
// Created by Zero on 06/08/2022.
//

#include "vulkan_texture.h"
#include "util.h"
#include "core/profiler.h"
#include "vulkan_device.h"
#include "core/image.h"
#include "core/image_base.h"

namespace ocarina {

VulkanTexture::VulkanTexture(
    VulkanDevice *device,
    Image *image,
    const TextureViewCreation &texture_view,
    const TextureSampler& sampler)
    : device_(device) {
    texture_sampler_ = sampler;
    init(image, texture_view);
}

VulkanTexture::VulkanTexture(
    VulkanDevice *device,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    PixelStorage format,
    const TextureViewCreation &texture_view,
    const TextureSampler& sampler,
    uint4 default_color,
    const void* data)
    : device_(device) {
    (void)default_color;
    (void)data;
    init_from_pixels(width, height, depth, format, texture_view, sampler);
}

VulkanTexture::VulkanTexture(VulkanDevice* device, uint32_t width, uint32_t height, PixelStorage format, TextureUsageFlags usage)
    : device_(device) {
    init_render_target(width, height, format, usage);
}

void VulkanTexture::init_render_target(uint32_t width, uint32_t height, PixelStorage format, TextureUsageFlags usage) {
    PROFILE_SCOPE();
    is_render_target_ = true;
    usage_flags_ = usage;
    res_.x = width;
    res_.y = height;
    res_.z = 1;
    pixel_storage_ = format;
    mip_levels_ = 1;

    const bool is_depth = (static_cast<uint32_t>(usage) & static_cast<uint32_t>(TextureUsageFlags::DepthStencil)) != 0;
    if (is_depth) {
        get_supported_depth_format(device_->physicalDevice(), &image_format_);
        aspect_mask_ = VK_IMAGE_ASPECT_DEPTH_BIT;
    } else {
        image_format_ = get_vulkan_format(format, false);
        aspect_mask_ = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    VkImageUsageFlags vk_usage = get_vulkan_image_usage_flags(static_cast<uint32_t>(usage));
    if (!is_depth && (vk_usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        vk_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if ((vk_usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0
        && (static_cast<uint32_t>(usage) & static_cast<uint32_t>(TextureUsageFlags::ShaderReadOnly)) != 0) {
        vk_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = image_format_;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = mip_levels_;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = vk_usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VK_CHECK_RESULT(vkCreateImage(device_->logicalDevice(), &image_info, nullptr, &image_));

    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(device_->logicalDevice(), image_, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = device_->get_memory_type(mem_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK_RESULT(vkAllocateMemory(device_->logicalDevice(), &alloc_info, nullptr, &image_memory_));
    VK_CHECK_RESULT(vkBindImageMemory(device_->logicalDevice(), image_, image_memory_, 0));

    create_render_target_image_view();

    if ((static_cast<uint32_t>(usage) & static_cast<uint32_t>(TextureUsageFlags::ShaderReadOnly)) != 0) {
        TextureSampler sampler;
        create_sampler(sampler);
    }
}

void VulkanTexture::create_render_target_image_view() {
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = image_format_;
    view_info.subresourceRange.aspectMask = aspect_mask_;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = mip_levels_;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    if (aspect_mask_ == VK_IMAGE_ASPECT_COLOR_BIT) {
        view_info.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A};
    }
    VK_CHECK_RESULT(vkCreateImageView(device_->logicalDevice(), &view_info, nullptr, &image_view_));
}

void VulkanTexture::init_from_pixels(
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    PixelStorage format,
    const TextureViewCreation &texture_view,
    const TextureSampler& sampler) {
    PROFILE_SCOPE();
    res_.x = width;
    res_.y = height;
    res_.z = depth;
    pixel_storage_ = format;
    image_format_ = get_vulkan_format(format, false);
    texture_sampler_ = sampler;

    uint32_t max_mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(res_.x, res_.y)))) + 1;
    mip_levels_ = texture_view.mip_level_count == 0 ? max_mip_levels : std::min(max_mip_levels, texture_view.mip_level_count);

    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = image_format_;
    image_info.mipLevels = mip_levels_;
    image_info.arrayLayers = texture_view.array_layer_count;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.extent = {static_cast<uint32_t>(res_.x), static_cast<uint32_t>(res_.y), static_cast<uint32_t>(res_.z)};
    image_info.usage = usage;

    VK_CHECK_RESULT(vkCreateImage(device_->logicalDevice(), &image_info, nullptr, &image_));

    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(device_->logicalDevice(), image_, &mem_requirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = mem_requirements.size;
    allocInfo.memoryTypeIndex = device_->get_memory_type(mem_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK_RESULT(vkAllocateMemory(device_->logicalDevice(), &allocInfo, nullptr, &image_memory_));

    VK_CHECK_RESULT(vkBindImageMemory(device_->logicalDevice(), image_, image_memory_, 0));

    create_image_view(texture_view);
    create_sampler(sampler);
}

void VulkanTexture::init(
    Image *image,
    const TextureViewCreation &texture_view) {
    pixel_storage_ = image->pixel_storage();
    init_from_pixels(
        image->resolution().x,
        image->resolution().y,
        1,
        image->pixel_storage(),
        texture_view,
        texture_sampler_);
}

void VulkanTexture::create_image_view(const TextureViewCreation &texture_view) {
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = image_format_;
    view_info.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A};
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = mip_levels_;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = texture_view.array_layer_count;
    VK_CHECK_RESULT(vkCreateImageView(device_->logicalDevice(), &view_info, nullptr, &image_view_));
}

void VulkanTexture::create_sampler(const TextureSampler &sampler_creation) {
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = get_vulkan_filter(sampler_creation.filter());
    sampler_info.minFilter = get_vulkan_filter(sampler_creation.filter());
    sampler_info.mipmapMode = get_vulkan_sampler_mipmap_mode(sampler_creation.mipmap_filter());
    sampler_info.addressModeU = get_vulkan_sampler_address(sampler_creation.u_address());
    sampler_info.addressModeV = get_vulkan_sampler_address(sampler_creation.v_address());
    sampler_info.addressModeW = get_vulkan_sampler_address(sampler_creation.w_address());
    sampler_info.mipLodBias = 0.0f;
    sampler_info.compareOp = VK_COMPARE_OP_NEVER;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = static_cast<float>(mip_levels_);
    sampler_info.maxAnisotropy = 4.0f;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VK_CHECK_RESULT(vkCreateSampler(device_->logicalDevice(), &sampler_info, nullptr, &sampler_));
}

VulkanTexture::~VulkanTexture() {
    if (image_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_->logicalDevice(), image_memory_, nullptr);
    }
    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_->logicalDevice(), image_, nullptr);
    }
    if (image_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_->logicalDevice(), image_view_, nullptr);
    }
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_->logicalDevice(), sampler_, nullptr);
    }
}
size_t VulkanTexture::data_size() const noexcept { return 0; }
size_t VulkanTexture::data_alignment() const noexcept { return 0; }
size_t VulkanTexture::max_member_size() const noexcept { return sizeof(handle_ty); }

}// namespace ocarina

#include "vulkan_cubemap.h"
#include "util.h"
#include "vulkan_device.h"
#include "core/profiler.h"
#include "math/basic_types.h"

namespace ocarina {

VulkanCubemap::VulkanCubemap(
    VulkanDevice *device,
    uint32_t width,
    uint32_t height,
    PixelStorage pixel_storage,
    const TextureSampler &sampler)
    : device_(device) {
    PROFILE_SCOPE();
    texture_sampler_ = sampler;
    face_res_ = make_uint2(width, height);
    pixel_storage_ = pixel_storage;
    image_format_ = get_vulkan_format(pixel_storage_, false);
    mip_levels_ = 1;

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = image_format_;
    image_info.extent = {face_res_.x, face_res_.y, 1};
    image_info.mipLevels = mip_levels_;
    image_info.arrayLayers = 6;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VK_CHECK_RESULT(vkCreateImage(device_->logicalDevice(), &image_info, nullptr, &image_));

    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(device_->logicalDevice(), image_, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = device_->get_memory_type(
        mem_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK_RESULT(vkAllocateMemory(device_->logicalDevice(), &alloc_info, nullptr, &image_memory_));
    VK_CHECK_RESULT(vkBindImageMemory(device_->logicalDevice(), image_, image_memory_, 0));

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    view_info.format = image_format_;
    view_info.components = {
        VK_COMPONENT_SWIZZLE_R,
        VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_B,
        VK_COMPONENT_SWIZZLE_A};
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = mip_levels_;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 6;
    VK_CHECK_RESULT(vkCreateImageView(device_->logicalDevice(), &view_info, nullptr, &image_view_));

    create_sampler(sampler);
}

void VulkanCubemap::create_sampler(const TextureSampler &sampler_creation) {
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
    sampler_info.maxAnisotropy = 1.0f;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VK_CHECK_RESULT(vkCreateSampler(device_->logicalDevice(), &sampler_info, nullptr, &sampler_));
}

VulkanCubemap::~VulkanCubemap() {
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_->logicalDevice(), sampler_, nullptr);
    }
    if (image_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_->logicalDevice(), image_view_, nullptr);
    }
    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_->logicalDevice(), image_, nullptr);
    }
    if (image_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_->logicalDevice(), image_memory_, nullptr);
    }
}

}// namespace ocarina

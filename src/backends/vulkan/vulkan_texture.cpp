//
// Created by Zero on 06/08/2022.
//

#include "vulkan_texture.h"
#include "util.h"
#include "vulkan_device.h"
#include "vulkan_driver.h"
#include "vulkan_command_buffer.h"
#include "core/image.h"
#include "core/image_base.h"
#include "vulkan_buffer.h"
#include "rhi/fence.h"
#include "rhi/command_buffer.h"

#include "stb_image_resize.h"

namespace ocarina {

namespace {

struct CpuMipChain {
    std::vector<uint8_t> pixels;
    std::vector<uint32_t> level_offsets;
    std::vector<uint32_t> level_widths;
    std::vector<uint32_t> level_heights;
};

size_t mip_level_byte_size(uint32_t width, uint32_t height, uint32_t channels) {
    return static_cast<size_t>(width) * height * channels;
}

bool resize_mip_level(const uint8_t* src, uint32_t src_w, uint32_t src_h, uint8_t* dst, uint32_t dst_w, uint32_t dst_h, uint32_t channels, PixelStorage format) {
    if (format == PixelStorage::BYTE4) {
        return stbir_resize_uint8_srgb(src, static_cast<int>(src_w), static_cast<int>(src_h), 0,
            dst, static_cast<int>(dst_w), static_cast<int>(dst_h), 0,
            static_cast<int>(channels), 3, 0) != 0;
    }
    if (is_8bit(format)) {
        return stbir_resize_uint8(src, static_cast<int>(src_w), static_cast<int>(src_h), 0,
            dst, static_cast<int>(dst_w), static_cast<int>(dst_h), 0,
            static_cast<int>(channels)) != 0;
    }
    return false;
}

CpuMipChain build_cpu_mip_chain(const void* base_pixels, uint32_t width, uint32_t height, uint32_t mip_levels, PixelStorage format) {
    PROFILE_SCOPE();
    const uint32_t channels = static_cast<uint32_t>(channel_num(format));
    const uint8_t* base = static_cast<const uint8_t*>(base_pixels);

    CpuMipChain chain;
    chain.level_offsets.reserve(mip_levels);
    chain.level_widths.reserve(mip_levels);
    chain.level_heights.reserve(mip_levels);

    size_t total_bytes = 0;
    uint32_t level_w = width;
    uint32_t level_h = height;
    for (uint32_t i = 0; i < mip_levels; ++i) {
        const uint32_t mip_w = std::max(level_w, 1u);
        const uint32_t mip_h = std::max(level_h, 1u);
        chain.level_offsets.push_back(static_cast<uint32_t>(total_bytes));
        chain.level_widths.push_back(mip_w);
        chain.level_heights.push_back(mip_h);
        total_bytes += mip_level_byte_size(mip_w, mip_h, channels);
        if (level_w > 1) {
            level_w /= 2;
        }
        if (level_h > 1) {
            level_h /= 2;
        }
    }

    chain.pixels.resize(total_bytes);
    std::memcpy(chain.pixels.data(), base, mip_level_byte_size(width, height, channels));

    uint32_t src_w = width;
    uint32_t src_h = height;
    for (uint32_t i = 1; i < mip_levels; ++i) {
        const uint32_t dst_w = chain.level_widths[i];
        const uint32_t dst_h = chain.level_heights[i];
        const uint8_t* src = chain.pixels.data() + chain.level_offsets[i - 1];
        uint8_t* dst = chain.pixels.data() + chain.level_offsets[i];
        if (!resize_mip_level(src, src_w, src_h, dst, dst_w, dst_h, channels, format)) {
            throw std::runtime_error("failed to generate CPU mipmaps with stb_image_resize");
        }
        src_w = dst_w;
        src_h = dst_h;
    }

    return chain;
}

void submit_texture_staging_upload(VulkanDevice* device, VulkanBuffer* staging_buffer, VulkanTexture* texture)
{
    CommandBuffer cmd = device->get_command_buffer(QueueType::Copy);
    cmd.begin();
    VulkanCommandBuffer* vk_cmd = static_cast<VulkanCommandBuffer*>(cmd.impl());
    vk_cmd->image_layout_barrier(texture, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vk_cmd->copy_image(staging_buffer, texture);
    vk_cmd->image_layout_barrier(texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    cmd.end();
    Fence fence = device->create_fence();
    cmd.submit_to_queue(QueueType::Copy, &fence);
    fence.wait();
    device->release_command_buffer(cmd);
    texture->set_image_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void submit_texture_mip_chain_upload(VulkanDevice* device, VulkanBuffer* staging_buffer, VulkanTexture* texture, const CpuMipChain& chain)
{
    std::vector<VkBufferImageCopy> regions(chain.level_offsets.size());
    for (size_t i = 0; i < chain.level_offsets.size(); ++i) {
        VkBufferImageCopy& region = regions[i];
        region.bufferOffset = chain.level_offsets[i];
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = static_cast<uint32_t>(i);
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {chain.level_widths[i], chain.level_heights[i], 1};
    }

    CommandBuffer cmd = device->get_command_buffer(QueueType::Copy);
    cmd.begin();
    VulkanCommandBuffer* vk_cmd = static_cast<VulkanCommandBuffer*>(cmd.impl());
    vk_cmd->image_layout_barrier(texture, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vk_cmd->copy_image(staging_buffer, texture, regions.data(), static_cast<uint32_t>(regions.size()));
    vk_cmd->image_layout_barrier(texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    cmd.end();
    Fence fence = device->create_fence();
    cmd.submit_to_queue(QueueType::Copy, &fence);
    fence.wait();
    device->release_command_buffer(cmd);
    texture->set_image_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void upload_texture_pixels(VulkanDevice* device, VulkanTexture* texture, const void* pixels, size_t base_level_bytes, uint32_t mip_levels, PixelStorage format)
{
    PROFILE_SCOPE();
    if (mip_levels <= 1) {
        VulkanBuffer staging_buffer(device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            base_level_bytes, pixels);
        submit_texture_staging_upload(device, &staging_buffer, texture);
        return;
    }

    if (!is_8bit(format)) {
        throw std::runtime_error("CPU mipmap generation only supports 8-bit pixel formats");
    }

    const CpuMipChain chain = build_cpu_mip_chain(pixels, texture->width(), texture->height(), mip_levels, format);
    VulkanBuffer staging_buffer(device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        chain.pixels.size(), chain.pixels.data());
    submit_texture_mip_chain_upload(device, &staging_buffer, texture, chain);
}

}// namespace

VulkanTexture::VulkanTexture(VulkanDevice *device, Image *image, const TextureViewCreation &texture_view, const TextureSampler& sampler)
    : device_(device) {
    texture_sampler_ = sampler;
    init(image, texture_view);
}

VulkanTexture::VulkanTexture(VulkanDevice *device, uint32_t width, uint32_t height, uint32_t depth, PixelStorage format, const TextureViewCreation &texture_view,
    const TextureSampler& sampler, uint4 default_color, const void* data)
    : device_(device) {
    init_from_pixels(width, height, depth, format, texture_view, sampler, default_color, data);
}

void VulkanTexture::init_from_pixels(uint32_t width, uint32_t height, uint32_t depth, PixelStorage format, const TextureViewCreation &texture_view,
    const TextureSampler& sampler, uint4 default_color, const void* data) {
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

    const size_t image_size = static_cast<size_t>(width) * height * depth * pixel_size(format);
    if (data != nullptr) {
        upload_texture_pixels(device_, this, data, image_size, mip_levels_, format);
    } else {
        std::vector<uint4> pixels(width * height * depth, default_color);
        upload_texture_pixels(device_, this, pixels.data(), pixels.size() * sizeof(uint4), mip_levels_, format);
    }

    create_image_view(texture_view);
    create_sampler(sampler);
}

void VulkanTexture::init(Image *image, const TextureViewCreation &texture_view) {
    pixel_storage_ = image->pixel_storage();
    init_from_pixels(
        image->resolution().x,
        image->resolution().y,
        1,
        image->pixel_storage(),
        texture_view,
        texture_sampler_,
        uint4(0, 0, 0, 255),
        image->pixel_ptr());
}

void VulkanTexture::load_cpu_data(Image *image) {
    load_cpu_data(image->pixel_ptr(), image->size_in_bytes());
}

void VulkanTexture::load_cpu_data(const void* data, size_t size_in_bytes) {
    upload_texture_pixels(device_, this, data, size_in_bytes, mip_levels_, pixel_storage_);
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

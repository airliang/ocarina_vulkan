//
// Created by Zero on 06/08/2022.
//

#pragma once

#include "rhi/descriptor_set.h"
#include <vulkan/vulkan.h>


namespace ocarina {
class VulkanShader;
class VulkanDescriptor;
class VulkanDescriptorImage;
class VulkanDescriptorSampler;
class VulkanDevice;
class VulkanDescriptorSet;
class VulkanBuffer;
class VulkanDescriptorSetWriter : public DescriptorSetWriter {
public:
    VulkanDescriptorSetWriter(VulkanDevice* device, VulkanDescriptorSet* descriptor_set);
    ~VulkanDescriptorSetWriter();
    void bind_buffer(uint32_t binding, VkDescriptorBufferInfo* buffer);
    void bind_texture(uint32_t binding,
        VkDescriptorImageInfo* texture,
        uint32_t element_index = 0,
        uint32_t texture_count = 1,
        VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    void bind_default_bindless_texture(uint32_t binding, uint32_t texture_count, VkDescriptorType descriptor_type);
    void bind_default_bindless_samplers(uint32_t binding, uint32_t sampler_count);
    void bind_default_texture(uint32_t binding, VkDescriptorType descriptor_type);
    void bind_default_sampler(uint32_t binding);
    void bind_sampler(uint32_t binding, VkDescriptorImageInfo* sampler, uint32_t element_index = 0, uint32_t sampler_count = 1);
    void build(VulkanDevice* device);

    void update_buffer(uint64_t name_id, const void *data, uint32_t size) override;
    void update_texture(uint64_t name_id, Texture *texture) override;
    void update_sampler(uint64_t name_id, VkSampler sampler);
    void update_bindless_texture_at_index(uint32_t index, Texture *texture);

private:
    std::unordered_map<uint64_t, VulkanDescriptor*> descriptors_;
    std::unordered_map<uint32_t, VulkanBuffer*> buffers_;
    std::vector<VkWriteDescriptorSet> writes_;
    std::vector<VkDescriptorImageInfo> image_infos_;
    /// Placeholder writes so a descriptor is valid before the app supplies a real resource.
    /// Capacity is reserved up front to keep pending write pointers stable.
    std::vector<VkDescriptorImageInfo> default_image_infos_;
    VulkanDescriptorSet *descriptor_set_ = nullptr;
    VulkanDescriptorImage *bindless_textures_descriptor_ = nullptr;
    VulkanDescriptorSampler *bindless_samplers_descriptor_ = nullptr;
};

}// namespace ocarina
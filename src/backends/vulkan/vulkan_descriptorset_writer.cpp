//
// Created by Zero on 06/08/2022.
//

#include "vulkan_descriptorset_writer.h"
#include "vulkan_shader.h"
#include "vulkan_buffer.h"
#include "vulkan_descriptorset.h"
#include "vulkan_device.h"
#include "vulkan_driver.h"
#include "vulkan_texture.h"
#include "vulkan_cubemap.h"
#include "util.h"

namespace ocarina {
VulkanDescriptorSetWriter::VulkanDescriptorSetWriter(VulkanDevice *device, VulkanDescriptorSet *descriptor_set) 
    : descriptor_set_(descriptor_set), device_(device) {
    VulkanDescriptorSetLayout *layout = descriptor_set->layout();
    size_t bindings_count = layout->get_bindings_count();
    for (size_t i = 0; i < bindings_count; ++i)
    {
        ShaderVariableBinding* binding = layout->get_binding(i);
        if (binding == nullptr) {
            continue;
        }
        if (binding->type == ShaderBindingType::UniformBuffer) {
            // UBO memory is owned by FrameResources / Material — only track the binding here.
            VulkanDescriptorBuffer *descriptor_buffer = ocarina::new_with_allocator<VulkanDescriptorBuffer>();
            descriptor_buffer->binding = binding->binding;
            descriptor_buffer->name_ = binding->name;
            descriptor_buffer->buffer_ = nullptr;
            descriptors_.insert(std::make_pair(hash64(descriptor_buffer->name_), descriptor_buffer));
        }
        else if (binding->type == ShaderBindingType::StorageBuffer) {
            VulkanDescriptorBuffer *descriptor_buffer = ocarina::new_with_allocator<VulkanDescriptorBuffer>();
            descriptor_buffer->binding = binding->binding;
            descriptor_buffer->name_ = binding->name;
            descriptor_buffer->buffer_ = nullptr;
            descriptors_.insert(std::make_pair(hash64(descriptor_buffer->name_), descriptor_buffer));
        }
        else if (binding->type == ShaderBindingType::CombinedImageSampler) {
            VulkanDescriptorImage *descriptor_image = ocarina::new_with_allocator<VulkanDescriptorImage>();
            descriptor_image->binding = binding->binding;
            descriptor_image->name_ = binding->name;
            descriptor_image->descriptor_type_ = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptor_image->default_sampler_name_ = std::string("sampler_") + binding->name;
            descriptors_.insert(std::make_pair(hash64(descriptor_image->name_), descriptor_image));
            if (binding->is_bindless) {
                bindless_textures_descriptor_ = descriptor_image;
            }
        } else if (binding->type == ShaderBindingType::SampledImage) {
            VulkanDescriptorImage *descriptor_image = ocarina::new_with_allocator<VulkanDescriptorImage>();
            descriptor_image->binding = binding->binding;
            descriptor_image->name_ = binding->name;
            descriptor_image->descriptor_type_ = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            descriptor_image->default_sampler_name_ = std::string("sampler_") + binding->name;
            descriptors_.insert(std::make_pair(hash64(descriptor_image->name_), descriptor_image));
            if (binding->is_bindless) {
                bindless_textures_descriptor_ = descriptor_image;
            }
        } else if (binding->type == ShaderBindingType::StorageImage) {
            VulkanDescriptorImage *descriptor_image = ocarina::new_with_allocator<VulkanDescriptorImage>();
            descriptor_image->binding = binding->binding;
            descriptor_image->name_ = binding->name;
            descriptor_image->descriptor_type_ = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            descriptors_.insert(std::make_pair(hash64(descriptor_image->name_), descriptor_image));
            if (binding->is_bindless) {
                bindless_textures_descriptor_ = descriptor_image;
            }
        } else if (binding->type == ShaderBindingType::Sampler) {
            VulkanDescriptorSampler *descriptor_sampler = ocarina::new_with_allocator<VulkanDescriptorSampler>();
            descriptor_sampler->binding = binding->binding;
            descriptor_sampler->name_ = binding->name;
            descriptors_.insert(std::make_pair(hash64(descriptor_sampler->name_), descriptor_sampler));
            if (binding->is_bindless) {
                bindless_samplers_descriptor_ = descriptor_sampler;
            }
        }
    }
}

VulkanDescriptorSetWriter::~VulkanDescriptorSetWriter()
{
    for (auto &descriptor : descriptors_) {
        if (descriptor.second) {
            if (descriptor.second->is_buffer_) {
                VulkanDescriptorBuffer *buffer_descriptor = static_cast<VulkanDescriptorBuffer *>(descriptor.second);
                ocarina::delete_with_allocator(buffer_descriptor);
            }
        }
    }
    descriptors_.clear();
}

void VulkanDescriptorSetWriter::bind_buffer(uint32_t binding, VkDescriptorBufferInfo* buffer)
{
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_set_->descriptor_set();
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = buffer;
    writes_.push_back(write);
}

void VulkanDescriptorSetWriter::bind_storage_buffer(uint32_t binding, VkDescriptorBufferInfo* buffer)
{
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_set_->descriptor_set();
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = buffer;
    writes_.push_back(write);
}

void VulkanDescriptorSetWriter::bind_texture(uint32_t binding,
    VkDescriptorImageInfo *texture,
    uint32_t element_index,
    uint32_t texture_count,
    VkDescriptorType descriptor_type) {
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_set_->descriptor_set();
    write.dstBinding = binding;
    write.dstArrayElement = element_index;
    write.descriptorCount = texture_count;
    write.descriptorType = descriptor_type;
    write.pImageInfo = texture;
    writes_.push_back(write);
}

void VulkanDescriptorSetWriter::bind_sampler(uint32_t binding, VkDescriptorImageInfo *sampler, uint32_t element_index, uint32_t sampler_count) {
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_set_->descriptor_set();
    write.dstBinding = binding;
    write.dstArrayElement = element_index;
    write.descriptorCount = sampler_count;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = sampler;
    writes_.push_back(write);
}

void VulkanDescriptorSetWriter::build(VulkanDevice *device) {
    if (writes_.empty() || device == nullptr) {
        return;
    }
    std::vector<VkWriteDescriptorSet> writes;

    for (auto &write : writes_) {
        writes.push_back(write);
    }

    vkUpdateDescriptorSets(device->logicalDevice(), writes.size(), writes.data(), 0, nullptr);
    writes_.clear();
}

void VulkanDescriptorSetWriter::update_buffer(
    uint64_t name_id,
    handle_ty buffer,
    uint32_t offset,
    uint32_t size) {
    auto it = descriptors_.find(name_id);
    if (it == descriptors_.end()) {
        return;
    }

    VulkanDescriptorBuffer *descriptor_buffer = static_cast<VulkanDescriptorBuffer *>(it->second);
    VulkanBuffer *vulkan_buffer = reinterpret_cast<VulkanBuffer *>(buffer);
    if (vulkan_buffer == nullptr || size == 0) {
        return;
    }

    descriptor_buffer->buffer_ = vulkan_buffer;
    buffer_infos_.push_back(VkDescriptorBufferInfo{});
    VkDescriptorBufferInfo& buffer_info = buffer_infos_.back();
    buffer_info.buffer = vulkan_buffer->buffer_handle();
    buffer_info.offset = offset;
    buffer_info.range = size;
    bind_buffer(descriptor_buffer->binding, &buffer_info);

    VulkanDevice *device = VulkanDriver::instance().get_device();
    build(device);
}

void VulkanDescriptorSetWriter::update_storage_buffer(
    uint64_t name_id,
    handle_ty buffer,
    uint64_t offset,
    uint64_t size) {
    auto it = descriptors_.find(name_id);
    if (it == descriptors_.end()) {
        return;
    }

    VulkanDescriptorBuffer *descriptor_buffer = static_cast<VulkanDescriptorBuffer *>(it->second);
    VulkanBuffer *vulkan_buffer = reinterpret_cast<VulkanBuffer *>(buffer);
    if (vulkan_buffer == nullptr) {
        return;
    }

    descriptor_buffer->buffer_ = vulkan_buffer;
    buffer_infos_.push_back(VkDescriptorBufferInfo{});
    VkDescriptorBufferInfo& buffer_info = buffer_infos_.back();
    buffer_info.buffer = vulkan_buffer->buffer_handle();
    buffer_info.offset = offset;
    buffer_info.range = size;
    bind_storage_buffer(descriptor_buffer->binding, &buffer_info);

    VulkanDevice *device = VulkanDriver::instance().get_device();
    build(device);
}

void VulkanDescriptorSetWriter::update_texture(uint64_t name_id, Texture *texture) {
    auto it = descriptors_.find(name_id);
    if (it != descriptors_.end()) {
        VulkanTexture *vulkan_texture = static_cast<VulkanTexture *>(texture->impl());
        VulkanDescriptorImage *descriptor_image = static_cast<VulkanDescriptorImage *>(it->second);
        const VkDescriptorType descriptor_type = descriptor_image->descriptor_type_;

        VkDescriptorImageInfo descriptor_info{};
        if (descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
            descriptor_info = vulkan_texture->get_storage_image_descriptor_info();
        } else if (descriptor_type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
            descriptor_info = vulkan_texture->get_sampled_image_descriptor_info();
        } else {
            descriptor_info = vulkan_texture->get_descriptor_info();
        }
        bind_texture(descriptor_image->binding, &descriptor_info, 0, 1, descriptor_type);

        //bind its sampler
        VkDescriptorImageInfo sampler_info{};
        uint64_t sampler_name_id = hash64(descriptor_image->default_sampler_name_);
        auto sampler_it = descriptors_.find(sampler_name_id);
        if (sampler_it != descriptors_.end()) {
            sampler_info.sampler = vulkan_texture->get_descriptor_info().sampler;
            if (sampler_info.sampler != VK_NULL_HANDLE) {
                VulkanDescriptorSampler *descriptor_sampler = static_cast<VulkanDescriptorSampler *>(sampler_it->second);
                bind_sampler(descriptor_sampler->binding, &sampler_info);
            }
        }

        VulkanDevice *device = VulkanDriver::instance().get_device();
        build(device);
    }
}

void VulkanDescriptorSetWriter::update_cubemap(uint64_t name_id, Cubemap *cubemap) {
    auto it = descriptors_.find(name_id);
    if (it == descriptors_.end() || cubemap == nullptr) {
        return;
    }

    VulkanCubemap *vulkan_cubemap = static_cast<VulkanCubemap *>(cubemap->impl());
    VulkanDescriptorImage *descriptor_image = static_cast<VulkanDescriptorImage *>(it->second);
    const VkDescriptorType descriptor_type = descriptor_image->descriptor_type_;

    VkDescriptorImageInfo descriptor_info = descriptor_type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
        ? vulkan_cubemap->get_sampled_image_descriptor_info()
        : vulkan_cubemap->get_descriptor_info();
    bind_texture(descriptor_image->binding, &descriptor_info, 0, 1, descriptor_type);

    VkDescriptorImageInfo sampler_info{};
    uint64_t sampler_name_id = hash64(descriptor_image->default_sampler_name_);
    auto sampler_it = descriptors_.find(sampler_name_id);
    if (sampler_it != descriptors_.end()) {
        sampler_info.sampler = vulkan_cubemap->get_descriptor_info().sampler;
        if (sampler_info.sampler != VK_NULL_HANDLE) {
            VulkanDescriptorSampler *descriptor_sampler = static_cast<VulkanDescriptorSampler *>(sampler_it->second);
            bind_sampler(descriptor_sampler->binding, &sampler_info);
        }
    }

    VulkanDevice *device = VulkanDriver::instance().get_device();
    build(device);
}

void VulkanDescriptorSetWriter::update_sampler(uint64_t name_id, VkSampler sampler)
{
    auto it = descriptors_.find(name_id);
    if (it != descriptors_.end()) {
        VulkanDescriptorSampler* descriptor_sampler = static_cast<VulkanDescriptorSampler*>(it->second);
        VkDescriptorImageInfo descriptor_info{};
        descriptor_info.sampler = sampler;
        descriptor_info.imageView = VK_NULL_HANDLE;   // ignored
        descriptor_info.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bind_sampler(descriptor_sampler->binding, &descriptor_info);

        VulkanDevice* device = VulkanDriver::instance().get_device();
        build(device);
    }
}

void VulkanDescriptorSetWriter::update_bindless_texture_at_index(uint32_t index, Texture *texture) {
    if (!bindless_textures_descriptor_ || texture == nullptr || index == InvalidUI32) {
        return;
    }

    if (index >= MAX_BINDLESS_TEXTURE_ARRAY_SIZE) {
        return;
    }

    VulkanTexture* vulkan_texture = static_cast<VulkanTexture*>(texture->impl());
    const VkDescriptorType descriptor_type = bindless_textures_descriptor_->descriptor_type_;
    VkDescriptorImageInfo descriptor_info = descriptor_type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
        ? vulkan_texture->get_sampled_image_descriptor_info()
        : vulkan_texture->get_descriptor_info();

    VkWriteDescriptorSet update{};
    update.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    update.dstSet = descriptor_set_->descriptor_set();
    update.dstBinding = bindless_textures_descriptor_->binding;
    update.dstArrayElement = index;
    update.descriptorType = descriptor_type;
    update.descriptorCount = 1;
    update.pImageInfo = &descriptor_info;

    VulkanDevice* device = VulkanDriver::instance().get_device();
    vkUpdateDescriptorSets(device->logicalDevice(), 1, &update, 0, nullptr);
}

void VulkanDescriptorSetWriter::update_bindless_sampler_at_index(uint32_t index, VkSampler sampler) {
    if (!bindless_samplers_descriptor_ || sampler == VK_NULL_HANDLE || index == InvalidUI32) {
        return;
    }

    if (index >= MAX_BINDLESS_SAMPLER_ARRAY_SIZE) {
        return;
    }

    VkDescriptorImageInfo descriptor_info{};
    descriptor_info.sampler = sampler;
    descriptor_info.imageView = VK_NULL_HANDLE;
    descriptor_info.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkWriteDescriptorSet update{};
    update.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    update.dstSet = descriptor_set_->descriptor_set();
    update.dstBinding = bindless_samplers_descriptor_->binding;
    update.dstArrayElement = index;
    update.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    update.descriptorCount = 1;
    update.pImageInfo = &descriptor_info;

    VulkanDevice* device = VulkanDriver::instance().get_device();
    vkUpdateDescriptorSets(device->logicalDevice(), 1, &update, 0, nullptr);
}

}// namespace ocarina

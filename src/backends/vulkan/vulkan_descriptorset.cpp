
#include "vulkan_device.h"
#include "vulkan_descriptorset.h"
#include "vulkan_shader.h"
#include "rhi/shader_program.h"
#include "util.h"
#include "vulkan_driver.h"
#include "vulkan_descriptorset_writer.h"

#include <cstring>

namespace ocarina {

VulkanDescriptorSet::VulkanDescriptorSet(VulkanDevice *device, VulkanDescriptorSetLayout *layout, VkDescriptorSet descriptor_set) : layout_(layout), 
device_(device), descriptor_set_(descriptor_set) {
    
    //set_is_global(layout->is_global_ubo());
    // Might want to create a "DescriptorPoolManager" class that handles this case, and builds
    // a new pool whenever an old pool fills up. But this is beyond our current scope

    writer_ = ocarina::new_with_allocator<VulkanDescriptorSetWriter>(device, this);
}

VulkanDescriptorSet::~VulkanDescriptorSet() {
    if (writer_) {
        ocarina::delete_with_allocator(writer_);
        writer_ = nullptr;
    }

    if (descriptor_set_ != VK_NULL_HANDLE) {
        layout_->free_descriptor_set(descriptor_set_);
    }
}

void VulkanDescriptorSet::update_buffer(uint64_t name_id, handle_ty buffer, uint32_t offset, uint32_t size) {
    if (writer_) {
        writer_->update_buffer(name_id, buffer, offset, size);
    }
}

void VulkanDescriptorSet::update_storage_buffer(
    uint64_t name_id,
    handle_ty buffer,
    uint64_t offset,
    uint64_t size) {
    if (writer_) {
        writer_->update_storage_buffer(name_id, buffer, offset, size);
    }
}

void VulkanDescriptorSet::update_texture(uint64_t name_id, Texture *texture) {
    //if bindless texture, we need to update the descriptor set
    ShaderVariableBinding* binding = layout_->get_binding_by_nameid(name_id);
    if (binding)
    {
        if (writer_) {
            writer_->update_texture(name_id, texture);
        }
        
    }
}

void VulkanDescriptorSet::update_sampler(uint64_t name_id, const TextureSampler& sampler) {
    ShaderVariableBinding* binding = layout_->get_binding_by_nameid(name_id);
    if (binding)
    {
        if (writer_) {
            VkSampler vk_sampler = VulkanDriver::instance().get_vulkan_sampler(sampler);
            writer_->update_sampler(name_id, vk_sampler);
        }
    }
}

void VulkanDescriptorSet::update_bindless_texture_at_index(uint32_t index, Texture *texture) {
    size_t binding_count = layout_->get_bindings_count();
    for (size_t i = 0; i < binding_count; ++i) {
        ShaderVariableBinding* binding = layout_->get_binding(i);
        if (binding && binding->is_bindless &&
            (binding->type == ShaderBindingType::CombinedImageSampler ||
             binding->type == ShaderBindingType::SampledImage)) {
            if (writer_) {
                writer_->update_bindless_texture_at_index(index, texture);
            }
            return;
        }
    }
}

void VulkanDescriptorSet::commit_updates() {
    if (writer_) {
        writer_->commit_updates();
    }
}

VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VulkanDevice *device, uint8_t descriptor_set_index) : device_(device), descriptor_set_index_(descriptor_set_index) {

}

VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
{
    for (auto& descriptor_set : allocated_descriptor_sets_)
    {
        if (descriptor_set) {
            ocarina::delete_with_allocator<VulkanDescriptorSet>(descriptor_set);
        }
    }
    allocated_descriptor_sets_.clear();
    if (descriptor_pool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device_->logicalDevice(), descriptor_pool_, nullptr);
    }

    if (layout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device_->logicalDevice(), layout_, nullptr);
    }
}

void VulkanDescriptorSetLayout::add_binding(const ShaderVariableBinding& binding) {
    const uint64_t nameid = hash64(binding.name);

    auto it = name_to_bindings_.find(nameid);
    if (it != name_to_bindings_.end()) {
        ShaderVariableBinding& existing = bindings_[it->second];
        existing.stage_flags |= binding.stage_flags;
        if (binding.size > existing.size) {
            existing.size = binding.size;
        }
        layout_built_ = false;
        hashkey_ = InvalidUI64;
        return;
    }

    name_to_bindings_.insert(std::make_pair(nameid, bindings_.size()));
    ShaderVariableBinding descriptor_binding = binding;

    if (descriptor_binding.is_bindless) {
        has_bindless_ = true;
        descriptor_binding.count = get_vulkan_bindless_resource_max_count(descriptor_binding.type);
    }

    bindings_.push_back(descriptor_binding);

    const VkDescriptorType descriptor_type = to_vulkan_descriptor_type(descriptor_binding.type);
    const uint32_t count = descriptor_binding.count;
    switch (descriptor_type) {
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        descriptor_count_.srv += count;
        break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        descriptor_count_.ubo += count;
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        descriptor_count_.uav += count;
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        descriptor_count_.samplers += count;
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        descriptor_count_.uav += count;
        break;
    }

    layout_built_ = false;
    hashkey_ = InvalidUI64;
}

bool VulkanDescriptorSetLayout::has_uniform_buffer_binding() const {
    for (const ShaderVariableBinding& binding : bindings_) {
        if (binding.type == ShaderBindingType::UniformBuffer) {
            return true;
        }
    }
    return false;
}

bool VulkanDescriptorSetLayout::has_storage_buffer_binding() const {
    for (const ShaderVariableBinding& binding : bindings_) {
        if (binding.type == ShaderBindingType::StorageBuffer) {
            return true;
        }
    }
    return false;
}

void VulkanDescriptorSetLayout::finalize_bindings() {
    if (has_bindless_) {
        usage_ = DescriptorSetUsage::BindlessArray;
        is_global_ubo_ = false;
        return;
    }

    if (descriptor_set_index_ == FRAME_SET && has_uniform_buffer_binding()) {
        usage_ = DescriptorSetUsage::GlobalSingleton;
        is_global_ubo_ = true;
        return;
    }

    usage_ = DescriptorSetUsage::PerInstance;
    is_global_ubo_ = false;
}

bool VulkanDescriptorSetLayout::build_layout()
{
    if (layout_built_)
        return false;
    if (layout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device_->logicalDevice(), layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }

    std::vector<VkDescriptorSetLayoutBinding> layout_bindings;
    std::vector<VkDescriptorBindingFlags> binding_flags;
    uint32_t binding_flag = 0;
    layout_bindings.reserve(bindings_.size());
    for (auto& it : bindings_)
    {
        VkDescriptorSetLayoutBinding descriptor_binding{};
        descriptor_binding.binding = it.binding;
        descriptor_binding.descriptorType = to_vulkan_descriptor_type(it.type);
        descriptor_binding.stageFlags = static_cast<VkShaderStageFlags>(it.stage_flags);
        descriptor_binding.descriptorCount = it.count;
        layout_bindings.push_back(descriptor_binding);

        if (it.is_bindless) {
            binding_flags.push_back(VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
                | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);
            //| VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT);

            binding_flag |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
        } 
        else if (has_bindless_)
        {
            binding_flags.push_back(0);
        }
    }

    uint32_t max_sets = 1;
    switch (usage_) {
    case DescriptorSetUsage::GlobalSingleton:
    case DescriptorSetUsage::BindlessArray:
        max_sets = 1;
        break;
    case DescriptorSetUsage::PerInstance:
        max_sets = kMaxPerInstanceDescriptorSets;
        break;
    }

    std::unordered_map<VkDescriptorType, uint32_t> type_totals;
    for (const ShaderVariableBinding& binding : bindings_) {
        const uint32_t per_set_count = binding.is_bindless
            ? get_vulkan_bindless_resource_max_count(binding.type)
            : binding.count;
        const uint32_t set_count = usage_ == DescriptorSetUsage::PerInstance ? max_sets : 1;
        type_totals[to_vulkan_descriptor_type(binding.type)] += per_set_count * set_count;
    }

    std::vector<VkDescriptorPoolSize> pool_sizes;
    pool_sizes.reserve(type_totals.size());
    for (const auto& entry : type_totals) {
        pool_sizes.push_back({
            .type = entry.first,
            .descriptorCount = entry.second,
        });
    }

    
    VkDescriptorSetLayoutBindingFlagsCreateInfoEXT binding_flags_info{};
    if (has_bindless_ && !binding_flags.empty())
    {
        binding_flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
        binding_flags_info.bindingCount = bindings_.size();
        binding_flags_info.pBindingFlags = binding_flags.data();
    }

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.pNext = nullptr;
    info.bindingCount = (uint32_t)layout_bindings.size();
    info.pBindings = layout_bindings.data();
    info.pNext = !binding_flags.empty() ? &binding_flags_info : nullptr;
    info.flags = binding_flag;

    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device_->logicalDevice(), &info, nullptr, &layout_));

    VkDescriptorPoolCreateInfo pool_create_info{};
    pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_create_info.pPoolSizes = pool_sizes.empty() ? nullptr : pool_sizes.data();
    pool_create_info.maxSets = std::max(max_sets, 1u);
    pool_create_info.flags = free_descriptor_set_ ? VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT : 0;

    if (info.flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT) {
        pool_create_info.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
    }

    VK_CHECK_RESULT(vkCreateDescriptorPool(device_->logicalDevice(), &pool_create_info, nullptr, &descriptor_pool_));
    pool_max_sets_count_ = pool_create_info.maxSets;
    layout_built_ = true;
    return true;
}

DescriptorSet *VulkanDescriptorSetLayout::allocate_descriptor_set() {
    allocated_sets_count_++;
    if (allocated_sets_count_ > pool_max_sets_count_ && descriptor_pool_ != VK_NULL_HANDLE)
    {
        assert(false);
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptor_pool_;
    VkDescriptorSetLayout layouts[1] = {layout_};
    allocInfo.pSetLayouts = layouts;
    allocInfo.descriptorSetCount = 1;

    if (has_bindless_)
    {
        uint32_t variableCounts[] = { MAX_BINDLESS_TEXTURE_ARRAY_SIZE };

        VkDescriptorSetVariableDescriptorCountAllocateInfo countInfo{};
        countInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        countInfo.descriptorSetCount = 1;
        countInfo.pDescriptorCounts = variableCounts;
    }

    VkDescriptorSet descriptor_set;
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device_->logicalDevice(), &allocInfo, &descriptor_set));
    VulkanDescriptorSet* vulkan_descriptor_set = ocarina::new_with_allocator<VulkanDescriptorSet>(device_, this, descriptor_set);
    allocated_descriptor_sets_.push_back(vulkan_descriptor_set);
    return vulkan_descriptor_set;
}

void VulkanDescriptorSetLayout::free_descriptor_set(VkDescriptorSet descriptor_set)
{
    if (free_descriptor_set_ && descriptor_pool_ != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_->logicalDevice(), descriptor_pool_, 1, &descriptor_set);
        allocated_sets_count_--;
    }
}

ShaderVariableBinding* VulkanDescriptorSetLayout::get_binding(uint64_t index) {
    //auto it = bindings_.find(binding);
    //if (it != bindings_.end()) {
    //    return &it->second;
    //}
    if (index >= bindings_.size())
        return nullptr;
    return &bindings_[index];
}

ShaderVariableBinding *VulkanDescriptorSetLayout::get_binding_by_nameid(uint64_t name_id) {
    auto it = name_to_bindings_.find(name_id);
    if (it != name_to_bindings_.end()) {
        uint32_t binding_index = it->second;
        return get_binding(binding_index);
    }
    return nullptr;
}

VulkanDescriptorPool::VulkanDescriptorPool(const DescriptorPoolCreation &creation, VulkanDevice *device) : device_(device), descriptor_pool_creation_(creation) {
    VkDescriptorPoolSize sizes[4];
    uint8_t npools = 0;

    if (creation.ubo > 0)
    {
        sizes[npools++] = {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = creation.ubo};
    }

    if (creation.srv > 0)
    {
        sizes[npools++] = {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = creation.srv};
    }

    if (creation.uav > 0)
    {
        sizes[npools++] = {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = creation.uav};
    }

    if (creation.samplers > 0)
    {
        sizes[npools++] = {
            .type = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = creation.samplers};
    }

    VkDescriptorPoolCreateInfo info;
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.poolSizeCount = npools;
    info.pPoolSizes = sizes;
    info.maxSets = 1;

    info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    vkCreateDescriptorPool(device_->logicalDevice(), &info, nullptr, &descriptor_pool_);
}

VulkanDescriptorPool::~VulkanDescriptorPool() {
    descriptor_sets.clear();
    vkDestroyDescriptorPool(device_->logicalDevice(), descriptor_pool_, nullptr);
}

VkDescriptorSet VulkanDescriptorPool::get_descriptor_set(VkDescriptorSetLayout layout) {
    auto it = descriptor_sets.find(layout);
    if (it != descriptor_sets.end())
    {
        return it->second;
    }
    
    // Creating a new set
    VkDescriptorSetLayout layouts[1] = {layout};
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = descriptor_pool_,
        .descriptorSetCount = 1,
        .pSetLayouts = layouts,
    };
    VkDescriptorSet vkSet;
    VkResult result = vkAllocateDescriptorSets(device_->logicalDevice(), &allocInfo, &vkSet);
    descriptor_sets.insert(std::make_pair(layout, vkSet));
    return vkSet;
}


std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>
VulkanDescriptorManager::create_or_get_descriptor_set_layouts(ShaderProgram* program) {
    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_layouts = {};

    for (auto& layout_key : cached_descriptor_set_layout_keys_) {
        layout_key.bindings.clear();
    }

    if (program != nullptr) {
        for (const ShaderVariableBinding& binding : program->variables()) {
            if (binding.descriptor_set == static_cast<uint8_t>(DescriptorSetIndex::FRAME_SET)) {
                continue;
            }
            ShaderVariableBinding program_binding = binding;
            if (program_binding.is_bindless) {
                program_binding.count = get_vulkan_bindless_resource_max_count(program_binding.type);
            }
            cached_descriptor_set_layout_keys_[binding.descriptor_set].add_binding(program_binding);
        }
    }

    for (auto& layout_key : cached_descriptor_set_layout_keys_) {
        layout_key.normalize();
        const uint64_t hashkey = layout_key.generate_hash();
        auto it = descriptor_set_layouts_.find(hashkey);
        if (it != descriptor_set_layouts_.end()) {
            descriptor_set_layouts[it->second->get_descriptor_set_index()] = it->second;
        } else if (!layout_key.bindings.empty()) {
            VulkanDescriptorSetLayout* layout = ocarina::new_with_allocator<VulkanDescriptorSetLayout>(
                device_,
                layout_key.bindings[0].descriptor_set);
            for (const auto& binding : layout_key.bindings) {
                layout->add_binding(binding);
                layout->set_name(binding.name);
            }
            descriptor_set_layouts[layout->get_descriptor_set_index()] = layout;
            descriptor_set_layouts_.insert(std::make_pair(hashkey, layout));
        }
    }

    if (frame_descriptor_set_layout_ != nullptr) {
        descriptor_set_layouts[static_cast<size_t>(DescriptorSetIndex::FRAME_SET)] =
            frame_descriptor_set_layout_;
    }

    for (size_t i = 0; i < descriptor_set_layouts.size(); ++i) {
        VulkanDescriptorSetLayout* layout =
            static_cast<VulkanDescriptorSetLayout*>(descriptor_set_layouts[i]);
        if (layout != nullptr) {
            layout->finalize_bindings();
            layout->build_layout();
        }
    }

    return descriptor_set_layouts;
}

DescriptorSetLayout* VulkanDescriptorManager::create_frame_descriptor_set_layout(
    span<const ShaderVariableBinding> bindings) {
    std::lock_guard<std::mutex> lock(layout_mutex_);
    if (frame_descriptor_set_layout_ != nullptr) {
        return frame_descriptor_set_layout_;
    }

    auto* layout = ocarina::new_with_allocator<VulkanDescriptorSetLayout>(
        device_,
        static_cast<uint8_t>(DescriptorSetIndex::FRAME_SET));

    for (const ShaderVariableBinding& binding : bindings) {
        layout->add_binding(binding);
        layout->set_name(binding.name);
    }
    layout->finalize_bindings();
    layout->build_layout();

    frame_descriptor_set_layout_ = layout;
    return frame_descriptor_set_layout_;
}

DescriptorSetLayout* VulkanDescriptorManager::get_frame_descriptor_set_layout() {
    std::lock_guard<std::mutex> lock(layout_mutex_);
    return frame_descriptor_set_layout_;
}

std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>
VulkanDescriptorManager::collect_shader_descriptor_set_layouts(ShaderProgram* program) {
    if (program == nullptr) {
        return {};
    }

    std::lock_guard<std::mutex> lock(layout_mutex_);
    return create_or_get_descriptor_set_layouts(program);
}

void VulkanDescriptorManager::clear()
{
    std::lock_guard<std::mutex> lock(layout_mutex_);
    for (auto layout : descriptor_set_layouts_)
    {
        VulkanDescriptorSetLayout* descriptor_set_layout = layout.second;
        ocarina::delete_with_allocator<VulkanDescriptorSetLayout>(descriptor_set_layout);
    }
    descriptor_set_layouts_.clear();

    if (frame_descriptor_set_layout_ != nullptr) {
        ocarina::delete_with_allocator(frame_descriptor_set_layout_);
        frame_descriptor_set_layout_ = nullptr;
    }
}

//VulkanDescriptorSetLayout* VulkanDescriptorManager::get_empty_descriptor_set_layout()
//{
//    if (empty_descriptor_set_layout_ == nullptr)
//    {
//        empty_descriptor_set_layout_ = ocarina::new_with_allocator<VulkanDescriptorSetLayout>(device_, 0);
//        empty_descriptor_set_layout_->build_layout();
//        descriptor_set_layouts_.insert(std::make_pair(0, empty_descriptor_set_layout_));
//    }
//    return empty_descriptor_set_layout_;
//}

}// namespace ocarina



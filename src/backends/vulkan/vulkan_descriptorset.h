//
// Created by Zero on 06/08/2022.
//

#pragma once

#include "core/stl.h"
#include "core/concepts.h"
#include "rhi/descriptor_set.h"
#include "rhi/shader_program.h"
#include <vulkan/vulkan.h>
#include "vulkan_shader.h"
#include <mutex>

namespace ocarina {

class VulkanDevice;
class ShaderProgram;
class VulkanBuffer;
class VulkanDescriptorSet;
class VulkanDescriptorSetWriter;

class VulkanDescriptor
{
public:
    virtual ~VulkanDescriptor() = default;
    uint32_t binding = 0;
    std::string name_;
    bool is_buffer_ = false;
};

class VulkanDescriptorBuffer : public VulkanDescriptor {
public:
    VulkanDescriptorBuffer()
    {
        is_buffer_ = true;
    }
    //VkDescriptorBufferInfo buffer_info = {};
    VulkanBuffer *buffer_ = nullptr;
    
};

class VulkanDescriptorImage : public VulkanDescriptor{
public:
    std::string default_sampler_name_;
    VkDescriptorType descriptor_type_ = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
};

class VulkanDescriptorSampler : public VulkanDescriptor {
};

class VulkanDescriptorPushConstants : public VulkanDescriptor {
};

enum class DescriptorSetUsage {
    PerInstance,
    GlobalSingleton,
    BindlessArray,
};

class VulkanDescriptorSetLayout : public DescriptorSetLayout {
    static constexpr uint8_t MAX_BINDINGS = 16;
    static constexpr uint32_t FRAME_SET = static_cast<uint32_t>(DescriptorSetIndex::FRAME_SET);
    static constexpr uint32_t kMaxPerInstanceDescriptorSets = 256;
public:
    VulkanDescriptorSetLayout(VulkanDevice* device, uint8_t descriptor_set_index);
    ~VulkanDescriptorSetLayout() override;
    void add_binding(const ShaderVariableBinding& binding);


    bool build_layout();

    void finalize_bindings();

    [[nodiscard]] DescriptorSetUsage usage() const { return usage_; }

    DescriptorCount get_descriptor_count() const noexcept {
        return descriptor_count_;
    }

    OC_MAKE_MEMBER_GETTER(layout, );
    //OC_MAKE_MEMBER_GETTER(descriptor_pool, );
    void set_is_global_ubo(bool is_global) {
        is_global_ubo_ = is_global;
    }

    DescriptorSet* allocate_descriptor_set() override;
    void free_descriptor_set(VkDescriptorSet descriptor_set);
    ShaderVariableBinding* get_binding(uint64_t index);
    size_t get_bindings_count() const override {
        return bindings_.size();
    }
    uint64_t get_binding_name_id(size_t index) const override {
        return index < bindings_.size() ? hash64(bindings_[index].name) : uint64_t(-1);
    }

    const char* get_binding_name(size_t index) const override {
        return index < bindings_.size() ? bindings_[index].name : "";
    }

    bool binding_is_uniform_buffer(size_t index) const override {
        return index < bindings_.size()
            && bindings_[index].type == ShaderBindingType::UniformBuffer;
    }

    bool free_descriptor_set() const {
        return free_descriptor_set_;
    }

    void set_free_descriptor_set(bool free_set) {
        free_descriptor_set_ = free_set;
    }

    uint32_t get_descriptor_set_index() const override {
        return descriptor_set_index_;
    }

    bool has_bindless_binding() const override { return has_bindless_; }

    bool has_uniform_buffer_binding() const override;

    bool has_storage_buffer_binding() const override;

    ShaderVariableBinding* get_binding_by_nameid(uint64_t name_id);

    uint64_t generate_hash() {
        if (hashkey_ != InvalidUI64) {
            return hashkey_;
        }
        hashkey_ = 0;
        uint32_t index = 1;
        for (const auto& it : bindings_) {
            const ShaderVariableBinding& binding = it;
            hashkey_ ^= std::hash<uint64_t>()(binding.binding) ^
                std::hash<uint64_t>()(binding.descriptor_set) ^
                std::hash<uint64_t>()(binding.count) ^
                std::hash<uint64_t>()(binding.stage_flags) ^
                std::hash<uint64_t>()(binding.size) ^
                std::hash<uint64_t>()(static_cast<uint8_t>(binding.type));
        }
        return hashkey_;
    }
private:
    DescriptorCount descriptor_count_;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    //VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;

    std::unordered_map<uint64_t, uint32_t> name_to_bindings_;
    std::vector<ShaderVariableBinding> bindings_;

    VulkanDevice* device_ = nullptr;

    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    bool layout_built_ = false;
    bool free_descriptor_set_ = false;  
    uint32_t allocated_sets_count_ = 0;
    uint32_t pool_max_sets_count_ = 1;
    uint8_t descriptor_set_index_ = 0;

    uint64_t hashkey_ = InvalidUI64;
    bool has_bindless_ = false;
    DescriptorSetUsage usage_ = DescriptorSetUsage::PerInstance;
    std::vector<VulkanDescriptorSet*> allocated_descriptor_sets_;
};

class VulkanDescriptorSet : public DescriptorSet {
private:
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VulkanDescriptorSetLayout *layout_ = nullptr;
    VulkanDevice *device_ = nullptr;
    VulkanDescriptorSetWriter *writer_ = nullptr;

public:
    VulkanDescriptorSet(VulkanDevice *device, VulkanDescriptorSetLayout* layout, VkDescriptorSet descriptor_set);
    ~VulkanDescriptorSet() override;
    OC_MAKE_MEMBER_GETTER(descriptor_set, );
    OC_MAKE_MEMBER_GETTER(layout, );
    //void copy_descriptors(VulkanDescriptor *descriptor);
    void update_buffer(uint64_t name_id, handle_ty buffer, uint32_t offset, uint32_t size) override;
    void update_storage_buffer(uint64_t name_id, handle_ty buffer, uint64_t offset, uint64_t size) override;
    void update_texture(uint64_t name_id, Texture *texture) override;
    void update_sampler(uint64_t name_id, const TextureSampler& sampler) override;
    void update_bindless_texture_at_index(uint32_t index, Texture *texture) override;
    void commit_updates() override;
    VulkanDescriptorSetLayout *get_layout() const {
        return layout_;
    }
};

struct DescriptorPoolCreation {
    uint32_t ubo;
    uint32_t srv;
    uint32_t uav;
    uint32_t samplers;

    bool operator==(DescriptorPoolCreation const &right) const noexcept {
        return ubo == right.ubo && srv == right.srv && uav == right.uav &&
               samplers == right.samplers;
    }

    DescriptorCount to_descriptor_count() const noexcept {
        DescriptorCount count;
        
        count.ubo = this->ubo;
        count.srv = this->srv;
        count.uav = this->uav;
        count.samplers = this->samplers;
        return count;
    }
};

class VulkanDescriptorPool : public concepts::Noncopyable
{
public:
    VulkanDescriptorPool(const DescriptorPoolCreation &creation, VulkanDevice* device);
    ~VulkanDescriptorPool();
    VkDescriptorSet get_descriptor_set(VkDescriptorSetLayout layout);
    OC_MAKE_MEMBER_GETTER(descriptor_pool, );
    OC_MAKE_MEMBER_GETTER(descriptor_pool_creation, );

    bool can_allocate(const DescriptorCount &count) const
    {
        return descriptor_pool_creation_.to_descriptor_count() == count;
    }
private :
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VulkanDevice *device_ = nullptr;
    std::map<VkDescriptorSetLayout, VkDescriptorSet> descriptor_sets;
    DescriptorPoolCreation descriptor_pool_creation_;
};


class VulkanDescriptorManager : public concepts::Noncopyable {
public:
    VulkanDescriptorManager(VulkanDevice *device) : device_(device) {
    }
    ~VulkanDescriptorManager(){};

    //VkDescriptorSet get_descriptor_set(const VulkanDescriptorSetLayout &layout, VulkanDevice *device);
    void clear();

    /// Create (or reuse cached) descriptor set layouts for a ShaderProgram from merged reflection.
    [[nodiscard]] std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>
    collect_shader_descriptor_set_layouts(ShaderProgram* program);

    struct DescriptorLayoutKey {
        std::vector<ShaderVariableBinding> bindings;

        // Sort bindings for deterministic comparison
        void normalize() {
            std::sort(bindings.begin(), bindings.end(), [](const auto &a, const auto &b) {
                return a.binding < b.binding;
            });
        }

        void add_binding(const ShaderVariableBinding& binding) {
            for (auto& existing : bindings) {
                if (existing.binding == binding.binding && existing.type == binding.type) {
                    existing.stage_flags |= binding.stage_flags;
                    if (binding.size > existing.size) {
                        existing.size = binding.size;
                    }
                    return;
                }
            }
            bindings.emplace_back(binding);
        }

        bool operator==(const DescriptorLayoutKey &other) const {
            if (bindings.size() != other.bindings.size()) {
                return false;
            }

            for (size_t i = 0; i < bindings.size(); ++i) {
                if (bindings[i].binding != other.bindings[i].binding ||
                    bindings[i].count != other.bindings[i].count ||
                    bindings[i].stage_flags != other.bindings[i].stage_flags ||
                    bindings[i].size != other.bindings[i].size ||
                    bindings[i].type != other.bindings[i].type) {
                    return false;
                }
            }

            return true;
        }

        void clear()
        {
            bindings.clear();
        }

        uint64_t generate_hash() {
            uint64_t hashkey = 0;
            for (const auto& binding : bindings) {
                hashkey ^= std::hash<uint64_t>()(binding.binding) ^
                    std::hash<uint64_t>()(binding.count) ^
                    std::hash<uint64_t>()(binding.stage_flags) ^
                    std::hash<uint64_t>()(binding.size) ^
                    std::hash<uint64_t>()(static_cast<uint8_t>(binding.type));
            }
            return hashkey;
        }
    };

    struct HashDescriptorLayoutKeyFunction {
        uint64_t operator()(const DescriptorLayoutKey &key) const {
            std::size_t h = 0;
            for (const auto &b : key.bindings) {
                h ^= std::hash<uint64_t>()(b.binding) ^
                     std::hash<uint64_t>()(b.count) ^
                     std::hash<uint64_t>()(b.stage_flags) ^
                     std::hash<uint64_t>()(b.size) ^
                     std::hash<uint64_t>()(static_cast<uint8_t>(b.type));
            }
            return h;
        }
    };

    //VulkanDescriptorSetLayout* get_empty_descriptor_set_layout();

    [[nodiscard]] std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>
    create_or_get_descriptor_set_layouts(ShaderProgram* program);

    [[nodiscard]] DescriptorSetLayout* create_frame_descriptor_set_layout(
        span<const ShaderVariableBinding> bindings);
    [[nodiscard]] DescriptorSetLayout* get_frame_descriptor_set_layout();

private:
    VulkanDevice *device_ = nullptr;
    VulkanDescriptorSetLayout *frame_descriptor_set_layout_ = nullptr;

    std::unordered_map<uint64_t, VulkanDescriptorSetLayout*> descriptor_set_layouts_;
    std::array<DescriptorLayoutKey, MAX_DESCRIPTOR_SETS_PER_SHADER> cached_descriptor_set_layout_keys_ = {};
    std::mutex layout_mutex_;
};

}// namespace ocarina
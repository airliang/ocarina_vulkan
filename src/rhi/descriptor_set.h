//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/concepts.h"

namespace ocarina {
struct RHIPipeline;
class Texture;
class TextureSampler;
class CommandBuffer;

class DescriptorSet : concepts::Noncopyable {
public:
    virtual ~DescriptorSet() {}

    /// Bind a uniform buffer to the named UBO binding (does not upload CPU data).
    virtual void update_buffer(uint64_t name_id, handle_ty buffer, uint32_t offset, uint32_t size) = 0;
    virtual void update_storage_buffer(uint64_t name_id, handle_ty buffer, uint64_t offset, uint64_t size) = 0;
    virtual void update_texture(uint64_t name_id, Texture *texture) = 0;
    virtual void update_sampler(uint64_t name_id, const TextureSampler& sampler) = 0;
    virtual void update_bindless_texture_at_index(uint32_t index, Texture *texture) = 0;

private:
};

class DescriptorSetLayout : concepts::Noncopyable {
public:
    DescriptorSetLayout() {}
    virtual ~DescriptorSetLayout() {}

    virtual DescriptorSet* allocate_descriptor_set() = 0;
    virtual size_t get_bindings_count() const { return 0; }
    virtual uint64_t get_binding_name_id(size_t index) const { return uint64_t(-1); }

    /// Vulkan descriptor set index from shader reflection (may be non-contiguous).
    virtual uint32_t get_descriptor_set_index() const { return 0; }
    virtual bool has_bindless_binding() const { return false; }
    virtual bool has_uniform_buffer_binding() const { return false; }
    virtual bool has_storage_buffer_binding() const { return false; }
    [[nodiscard]] virtual const char* get_binding_name(size_t index) const {
        (void)index;
        return "";
    }
    [[nodiscard]] virtual bool binding_is_uniform_buffer(size_t index) const {
        (void)index;
        return false;
    }

    const std::string get_name() const { return name_; }
    void set_name(const std::string &name) {
        name_ = name;
    }

    bool is_global_ubo() const { return is_global_ubo_; }
    bool is_global_textures() const { return is_global_textures_; }
    void set_is_global_textures(bool is_global) { is_global_textures_ = is_global; }

private:
    std::string name_;

protected:
    bool is_global_ubo_ = false;
    bool is_global_textures_ = false;
};

class DescriptorSetWriter : concepts::Noncopyable {
public:
    DescriptorSetWriter() {}
    virtual ~DescriptorSetWriter() {}
    virtual void update_buffer(uint64_t name_id, handle_ty buffer, uint32_t offset, uint32_t size) = 0;
    virtual void update_texture(uint64_t name_id, Texture* texture) = 0;

protected:
    DescriptorSetLayout *descriptor_set_layout_ = nullptr;
};

}// namespace ocarina

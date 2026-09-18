#include "shader_parameters.h"

#include "resource_manager.h"
#include "rhi/command_buffer.h"
#include "rhi/descriptor_set.h"
#include "rhi/device.h"
#include "rhi/pipeline_state.h"
#include "core/logging.h"

#include <algorithm>
#include <cstring>

namespace ocarina {

namespace {

constexpr uint32_t kFrameSetIndex = static_cast<uint32_t>(DescriptorSetIndex::FRAME_SET);

[[nodiscard]] bool is_local_set(uint8_t set_index) noexcept {
    return set_index != static_cast<uint8_t>(kFrameSetIndex);
}

} // namespace

ShaderParameters::ShaderParameters(Device* device, ShaderProgram* shader_program)
    : device_(device), shader_program_(shader_program) {
    init_from_reflection();
}

ShaderParameters::~ShaderParameters() {
    release_gpu_buffers();
}

void ShaderParameters::release_gpu_buffers() {
    ResourceManager& resources = ResourceManager::instance();
    for (auto& [name_id, ubo] : uniform_buffers_) {
        (void)name_id;
        if (ubo.buffer.handle() != 0) {
            resources.release_buffer(ubo.buffer.handle());
            ubo.buffer.reset();
        }
        ubo.descriptor_bound = false;
        ubo.dirty = false;
    }
    cached_descriptor_values_.clear();
}

void ShaderParameters::init_from_reflection() {
    properties_.clear();
    property_indices_.clear();
    uniform_buffers_.clear();
    cached_descriptor_values_.clear();
    descriptor_set_layouts_.fill(nullptr);
    descriptor_sets_.fill(nullptr);

    if (shader_program_ == nullptr) {
        return;
    }

    const auto& layouts = shader_program_->descriptor_set_layouts();
    for (size_t i = 0; i < layouts.size(); ++i) {
        if (i == kFrameSetIndex) {
            continue;
        }
        descriptor_set_layouts_[i] = layouts[i];
    }

    for (const ShaderVariableBinding& binding : shader_program_->variables()) {
        if (!is_local_set(binding.descriptor_set) || binding.name[0] == '\0') {
            continue;
        }

        const uint64_t name_id = hash64(binding.name);
        if (property_indices_.find(name_id) != property_indices_.end()) {
            continue;
        }

        Property property;
        property.name = binding.name;
        property.binding_type = binding.type;
        property.size = binding.size;
        property.binding = binding.binding;
        property.descriptor_set = binding.descriptor_set;

        switch (binding.type) {
            case ShaderBindingType::UniformBuffer: {
                property.kind = PropertyKind::UniformBuffer;
                property.uniform_buffer_name_id = name_id;

                std::vector<ShaderProgram::UniformBufferMember> members;
                uint32_t buffer_size = 0;
                if (shader_program_->get_uniform_buffer_members(binding.name, members, buffer_size)
                    && buffer_size > 0) {
                    property.size = buffer_size;
                }

                OwnedUniformBuffer owned;
                owned.size = property.size;
                if (owned.size > 0) {
                    owned.cpu_data.assign(owned.size, 0);
                    owned.dirty = true;
                    uniform_buffers_.emplace(name_id, std::move(owned));
                }

                property_indices_.emplace(name_id, properties_.size());
                properties_.push_back(property);

                for (const ShaderProgram::UniformBufferMember& member : members) {
                    Property member_property;
                    member_property.name = member.name;
                    member_property.kind = PropertyKind::UniformMember;
                    member_property.type = member.type;
                    member_property.binding_type = ShaderBindingType::UniformBuffer;
                    member_property.size = member.size;
                    member_property.offset = member.offset;
                    member_property.uniform_buffer_name_id = name_id;
                    member_property.binding = binding.binding;
                    member_property.descriptor_set = binding.descriptor_set;
                    property_indices_.emplace(hash64(member.name), properties_.size());
                    properties_.push_back(std::move(member_property));
                }
                break;
            }
            case ShaderBindingType::SampledImage:
            case ShaderBindingType::CombinedImageSampler:
            case ShaderBindingType::StorageImage:
                property.kind = PropertyKind::Texture;
                property_indices_.emplace(name_id, properties_.size());
                properties_.push_back(std::move(property));
                break;
            case ShaderBindingType::Sampler:
                property.kind = PropertyKind::Sampler;
                property_indices_.emplace(name_id, properties_.size());
                properties_.push_back(std::move(property));
                break;
            case ShaderBindingType::StorageBuffer:
                property.kind = PropertyKind::StorageBuffer;
                property.uniform_buffer_name_id = name_id;
                property_indices_.emplace(name_id, properties_.size());
                properties_.push_back(std::move(property));
                break;
            default:
                break;
        }
    }
}

void ShaderParameters::allocate_descriptor_sets() {
    if (shader_program_ == nullptr) {
        return;
    }

    for (uint32_t set_index = 0; set_index < MAX_DESCRIPTOR_SETS_PER_SHADER; ++set_index) {
        if (set_index == kFrameSetIndex || descriptor_sets_[set_index] != nullptr) {
            continue;
        }
        DescriptorSetLayout* layout = descriptor_set_layouts_[set_index];
        if (layout == nullptr) {
            layout = shader_program_->descriptor_set_layouts()[set_index];
            if (layout == nullptr || set_index == kFrameSetIndex) {
                continue;
            }
            descriptor_set_layouts_[set_index] = layout;
        }
        descriptor_sets_[set_index] = layout->allocate_descriptor_set();
    }
}

void ShaderParameters::ensure_uniform_buffer_gpus() {
    if (device_ == nullptr) {
        return;
    }

    for (auto& [name_id, ubo] : uniform_buffers_) {
        if (ubo.buffer.handle() != 0 || ubo.size == 0) {
            continue;
        }
        const Property* property = find_property(name_id);
        const char* buffer_name = property != nullptr ? property->name.c_str() : "shader_uniform";
        ubo.buffer = ResourceManager::instance().create_buffer<std::byte>(
            device_,
            ubo.size,
            GraphicBufferBindFlags::ConstantBuffer,
            buffer_name);
        ubo.descriptor_bound = false;
        ubo.dirty = true;
    }
}

void ShaderParameters::ensure_gpu_ready() {
    allocate_descriptor_sets();
    ensure_uniform_buffer_gpus();
}

bool ShaderParameters::is_ready() {
    ensure_gpu_ready();
    for (uint32_t set_index = 0; set_index < MAX_DESCRIPTOR_SETS_PER_SHADER; ++set_index) {
        if (set_index == kFrameSetIndex) {
            continue;
        }
        if (descriptor_set_layouts_[set_index] != nullptr && descriptor_sets_[set_index] == nullptr) {
            return false;
        }
    }
    for (const auto& [name_id, ubo] : uniform_buffers_) {
        (void)name_id;
        if (ubo.size > 0 && ubo.buffer.handle() == 0) {
            return false;
        }
    }
    return true;
}

void ShaderParameters::upload_owned_uniform_buffer(uint64_t name_id, OwnedUniformBuffer& ubo) {
    if (ubo.buffer.handle() == 0 || ubo.size == 0) {
        return;
    }

    const Property* property = find_property(name_id);
    if (property == nullptr) {
        return;
    }
    DescriptorSet* descriptor_set = find_descriptor_set_for_property(*property);
    if (descriptor_set == nullptr) {
        return;
    }

    ubo.buffer.copy_from_immediately(ubo.cpu_data.data(), ubo.size);
    if (!ubo.descriptor_bound) {
        descriptor_set->update_buffer(name_id, ubo.buffer.handle(), 0, ubo.size);
        ubo.descriptor_bound = true;
    }
    ubo.dirty = false;
}

void ShaderParameters::apply_uploads() {
    ensure_gpu_ready();
    for (auto& [name_id, ubo] : uniform_buffers_) {
        if (!ubo.dirty && ubo.descriptor_bound) {
            continue;
        }
        if (ubo.buffer.handle() == 0) {
            continue;
        }
        upload_owned_uniform_buffer(name_id, ubo);
    }
}

void ShaderParameters::bind(CommandBuffer& cmd, const RHIPipeline* pipeline) {
    if (pipeline == nullptr || pipeline->pipeline_layout == 0
        || pipeline->pipeline_layout == InvalidUI64) {
        return;
    }

    apply_uploads();

    for (uint32_t set_index = 0; set_index < MAX_DESCRIPTOR_SETS_PER_SHADER; ++set_index) {
        if (set_index == kFrameSetIndex) {
            continue;
        }
        DescriptorSet* descriptor_set = descriptor_sets_[set_index];
        if (descriptor_set == nullptr) {
            continue;
        }
        cmd.bind_descriptor_sets(&descriptor_set, set_index, 1, pipeline->pipeline_layout);
    }
}

const ShaderParameters::Property* ShaderParameters::find_property(uint64_t name_id) const noexcept {
    const auto it = property_indices_.find(name_id);
    if (it == property_indices_.end()) {
        return nullptr;
    }
    return &properties_[it->second];
}

ShaderParameters::OwnedUniformBuffer* ShaderParameters::find_owned_uniform_buffer(uint64_t name_id) noexcept {
    const auto it = uniform_buffers_.find(name_id);
    if (it == uniform_buffers_.end()) {
        return nullptr;
    }
    return &it->second;
}

DescriptorSet* ShaderParameters::find_descriptor_set_for_property(const Property& property) const noexcept {
    if (property.descriptor_set >= MAX_DESCRIPTOR_SETS_PER_SHADER) {
        return nullptr;
    }
    return descriptor_sets_[property.descriptor_set];
}

bool ShaderParameters::cache_descriptor_value(
    uint64_t name_id,
    handle_ty resource,
    uint64_t offset,
    uint64_t size) noexcept {
    const auto it = cached_descriptor_values_.find(name_id);
    if (it != cached_descriptor_values_.end()
        && it->second.resource == resource
        && it->second.offset == offset
        && it->second.size == size) {
        return false;
    }
    cached_descriptor_values_[name_id] = CachedDescriptorValue{resource, offset, size};
    return true;
}

DescriptorSet* ShaderParameters::descriptor_set(uint32_t set_index) const noexcept {
    if (set_index >= MAX_DESCRIPTOR_SETS_PER_SHADER) {
        return nullptr;
    }
    return descriptor_sets_[set_index];
}

uint32_t ShaderParameters::local_descriptor_set_count() const noexcept {
    uint32_t count = 0;
    for (uint32_t set_index = 0; set_index < MAX_DESCRIPTOR_SETS_PER_SHADER; ++set_index) {
        if (set_index != kFrameSetIndex && descriptor_sets_[set_index] != nullptr) {
            ++count;
        }
    }
    return count;
}

void ShaderParameters::write_uniform_member(uint64_t name_id, const void* data, size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }
    const Property* property = find_property(name_id);
    if (property == nullptr || property->kind != PropertyKind::UniformMember) {
        return;
    }
    OwnedUniformBuffer* ubo = find_owned_uniform_buffer(property->uniform_buffer_name_id);
    if (ubo == nullptr || ubo->cpu_data.empty()) {
        return;
    }
    const size_t copy_size = std::min(size, static_cast<size_t>(property->size));
    if (property->offset + copy_size > ubo->cpu_data.size()) {
        return;
    }
    std::memcpy(ubo->cpu_data.data() + property->offset, data, copy_size);
    ubo->dirty = true;
}

void ShaderParameters::set_buffer(const char* name, handle_ty buffer, uint64_t offset, uint64_t size) {
    if (name == nullptr) {
        return;
    }
    set_buffer(hash64(name), buffer, offset, size);
}

void ShaderParameters::set_buffer(uint64_t name_id, handle_ty buffer, uint64_t offset, uint64_t size) {
    ensure_gpu_ready();
    const Property* property = find_property(name_id);
    if (property == nullptr || property->kind != PropertyKind::StorageBuffer || buffer == 0) {
        return;
    }
    DescriptorSet* descriptor_set = find_descriptor_set_for_property(*property);
    if (descriptor_set == nullptr) {
        return;
    }
    if (!cache_descriptor_value(name_id, buffer, offset, size)) {
        return;
    }
    descriptor_set->update_storage_buffer(name_id, buffer, offset, size);
}

void ShaderParameters::set_texture(const char* name, Texture* texture) {
    if (name == nullptr) {
        return;
    }
    set_texture(hash64(name), texture);
}

void ShaderParameters::set_texture(uint64_t name_id, Texture* texture) {
    ensure_gpu_ready();
    const Property* property = find_property(name_id);
    if (property == nullptr || property->kind != PropertyKind::Texture || texture == nullptr) {
        return;
    }
    DescriptorSet* descriptor_set = find_descriptor_set_for_property(*property);
    if (descriptor_set == nullptr) {
        return;
    }
    const handle_ty texture_handle = reinterpret_cast<handle_ty>(texture);
    if (!cache_descriptor_value(name_id, texture_handle)) {
        return;
    }
    descriptor_set->update_texture(name_id, texture);
}

void ShaderParameters::set_uniform_buffer(
    const char* name,
    handle_ty buffer,
    uint32_t offset,
    uint32_t size) {
    if (name == nullptr) {
        return;
    }
    set_uniform_buffer(hash64(name), buffer, offset, size);
}

void ShaderParameters::set_uniform_buffer(
    uint64_t name_id,
    handle_ty buffer,
    uint32_t offset,
    uint32_t size) {
    ensure_gpu_ready();
    const Property* property = find_property(name_id);
    if (property == nullptr || property->kind != PropertyKind::UniformBuffer || buffer == 0) {
        return;
    }
    DescriptorSet* descriptor_set = find_descriptor_set_for_property(*property);
    if (descriptor_set == nullptr) {
        return;
    }
    if (!cache_descriptor_value(name_id, buffer, offset, size)) {
        return;
    }
    // Prefer external buffer over owned CPU staging for this binding.
    if (OwnedUniformBuffer* owned = find_owned_uniform_buffer(name_id)) {
        owned->descriptor_bound = true;
        owned->dirty = false;
    }
    descriptor_set->update_buffer(name_id, buffer, offset, size);
}

void ShaderParameters::set_float(const char* name, float value) {
    if (name == nullptr) {
        return;
    }
    write_uniform_member(hash64(name), &value, sizeof(value));
}

void ShaderParameters::set_int(const char* name, int32_t value) {
    if (name == nullptr) {
        return;
    }
    write_uniform_member(hash64(name), &value, sizeof(value));
}

void ShaderParameters::set_vector(const char* name, const float2& value) {
    if (name == nullptr) {
        return;
    }
    write_uniform_member(hash64(name), &value, sizeof(value));
}

void ShaderParameters::set_vector(const char* name, const float3& value) {
    if (name == nullptr) {
        return;
    }
    write_uniform_member(hash64(name), &value, sizeof(value));
}

void ShaderParameters::set_vector(const char* name, const float4& value) {
    if (name == nullptr) {
        return;
    }
    write_uniform_member(hash64(name), &value, sizeof(value));
}

void ShaderParameters::set_vector(const char* name, const int2& value) {
    if (name == nullptr) {
        return;
    }
    write_uniform_member(hash64(name), &value, sizeof(value));
}

void ShaderParameters::set_vector(const char* name, const int3& value) {
    if (name == nullptr) {
        return;
    }
    write_uniform_member(hash64(name), &value, sizeof(value));
}

void ShaderParameters::set_vector(const char* name, const int4& value) {
    if (name == nullptr) {
        return;
    }
    write_uniform_member(hash64(name), &value, sizeof(value));
}

void ShaderParameters::set_matrix(const char* name, const float4x4& value) {
    if (name == nullptr) {
        return;
    }
    write_uniform_member(hash64(name), &value, sizeof(value));
}

}// namespace ocarina

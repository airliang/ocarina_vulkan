#include "material.h"
#include "core/hash.h"
#include "core/logging.h"
#include "resource_manager.h"
#include "rhi/descriptor_set.h"
#include "rhi/device.h"
#include "rhi/pipeline_state.h"
#include "rhi/shader_base.h"
#include "rhi/shader_program.h"
#include "rhi/resources/resource.h"
#include "framework/frame_resources.h"
#include <algorithm>

namespace ocarina {
namespace {

constexpr uint32_t kFragmentShaderStageFlag = 16u; // VK_SHADER_STAGE_FRAGMENT_BIT

[[nodiscard]] bool descriptor_set_used_in_pixel_shader(
    const ShaderProgram* program,
    uint32_t set_index) noexcept {
    if (program == nullptr) {
        return false;
    }
    for (const ShaderVariableBinding& binding : program->variables()) {
        if (binding.descriptor_set != set_index) {
            continue;
        }
        if ((binding.stage_flags & kFragmentShaderStageFlag) != 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool is_material_local_descriptor_set_layout(
    const ShaderProgram* program,
    const DescriptorSetLayout* layout) noexcept {
    if (layout == nullptr) {
        return false;
    }
    const uint32_t set_index = layout->get_descriptor_set_index();
    if (set_index == static_cast<uint32_t>(DescriptorSetIndex::FRAME_SET)) {
        return false;
    }
    return descriptor_set_used_in_pixel_shader(program, set_index);
}

void collect_non_global_descriptor_set_layouts(
    const ShaderProgram* program,
    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& out_layouts) {
    if (program == nullptr) {
        return;
    }
    for (DescriptorSetLayout* layout : program->descriptor_set_layouts()) {
        if (!is_material_local_descriptor_set_layout(program, layout)) {
            continue;
        }
        const uint32_t set_index = layout->get_descriptor_set_index();
        if (set_index < out_layouts.size()) {
            out_layouts[set_index] = layout;
        }
    }
}

std::vector<RHIShader::UniformBufferMember> to_rhi_uniform_members(
    const std::vector<ShaderProgram::UniformBufferMember>& members) {
    std::vector<RHIShader::UniformBufferMember> out;
    out.reserve(members.size());
    for (const ShaderProgram::UniformBufferMember& member : members) {
        RHIShader::UniformBufferMember converted;
        converted.name = member.name;
        converted.type = member.type;
        converted.size = member.size;
        converted.offset = member.offset;
        out.push_back(std::move(converted));
    }
    return out;
}

}// namespace

Material::Material(Device* device, ShaderProgram* shader_program) : device_(device), shader_program_(shader_program) {
    pipeline_state_ = PipelineState::MakeGraphicsDefault(0, 0);

    collect_non_global_descriptor_set_layouts(shader_program, descriptor_set_layouts_);
    // Resolve layout only. VkDescriptorSet allocation happens on the render thread.
    resolve_material_descriptor_layout();

    init_material_properties(shader_program);
}

Material::~Material() {
    release_gpu_buffers();
}

void Material::release_gpu_buffers() {
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
}

void Material::apply_reflected_members(
    uint64_t buffer_name_id,
    uint32_t buffer_size,
    const std::vector<RHIShader::UniformBufferMember>& members) {
    (void)buffer_size;
    for (const RHIShader::UniformBufferMember& member : members) {
        MaterialProperty property;
        property.name = member.name;
        property.kind = PropertyKind::UniformMember;
        property.type = member.type;
        property.binding_type = ShaderBindingType::UniformBuffer;
        property.size = member.size;
        property.offset = member.offset;
        property.uniform_buffer_name_id = buffer_name_id;
        material_property_indices_.emplace(hash64(property.name), material_properties_.size());
        material_properties_.push_back(std::move(property));
    }
}

void Material::add_binding_property(
    const ShaderVariableBinding& binding,
    PropertyKind kind) {
    if (binding.name[0] == '\0') {
        return;
    }

    const uint64_t name_id = hash64(binding.name);
    if (material_property_indices_.find(name_id) != material_property_indices_.end()) {
        return;
    }

    MaterialProperty property;
    property.name = binding.name;
    property.kind = kind;
    property.binding_type = binding.type;
    property.size = binding.size;
    property.binding = binding.binding;
    property.descriptor_set = binding.descriptor_set;
    if (kind == PropertyKind::UniformBuffer || kind == PropertyKind::StorageBuffer) {
        property.uniform_buffer_name_id = name_id;
    }
    material_property_indices_.emplace(name_id, material_properties_.size());
    material_properties_.push_back(std::move(property));
}

void Material::add_uniform_buffer_property(
    const char* binding_name,
    uint32_t buffer_size,
    const std::vector<RHIShader::UniformBufferMember>& members,
    bool create_owned_buffer,
    uint8_t binding,
    uint8_t descriptor_set) {
    if (binding_name == nullptr || binding_name[0] == '\0' || buffer_size == 0) {
        return;
    }

    const uint64_t name_id = hash64(binding_name);
    if (material_property_indices_.find(name_id) != material_property_indices_.end()) {
        return;
    }

    MaterialProperty ubo_property;
    ubo_property.name = binding_name;
    ubo_property.kind = PropertyKind::UniformBuffer;
    ubo_property.binding_type = ShaderBindingType::UniformBuffer;
    ubo_property.size = buffer_size;
    ubo_property.uniform_buffer_name_id = name_id;
    ubo_property.binding = binding;
    ubo_property.descriptor_set = descriptor_set;
    material_property_indices_.emplace(name_id, material_properties_.size());
    material_properties_.push_back(std::move(ubo_property));

    if (material_params_buffer_name_id_ == 0) {
        material_params_buffer_name_id_ = name_id;
        material_params_byte_size_ = buffer_size;
    }

    apply_reflected_members(name_id, buffer_size, members);

    if (create_owned_buffer) {
        OwnedUniformBuffer owned;
        owned.size = buffer_size;
        owned.cpu_data.assign(buffer_size, 0);
        owned.dirty = true;
        uniform_buffers_.emplace(name_id, std::move(owned));
    }
}

void Material::ensure_gpu_shaders() {
    if (device_ == nullptr || shader_program_ == nullptr) {
        return;
    }

    shader_program_->ensure_gpu_shaders(device_);
    if (pipeline_state_.shaders[0] == 0) {
        pipeline_state_.shaders[0] = shader_program_->shader_handle(ShaderType::VertexShader);
    }
    if (pipeline_state_.shaders[1] == 0) {
        pipeline_state_.shaders[1] = shader_program_->shader_handle(ShaderType::PixelShader);
    }
}

void Material::init_material_properties(ShaderProgram* shader_program) {
    if (shader_program == nullptr) {
        return;
    }

    material_properties_.clear();
    material_property_indices_.clear();
    uniform_buffers_.clear();
    material_params_buffer_name_id_ = 0;
    material_params_byte_size_ = 0;

    std::vector<ShaderProgram::UniformBufferMember> members;
    uint32_t buffer_size = 0;

    for (const ShaderVariableBinding& binding : shader_program->variables()) {
        if (binding.descriptor_set == static_cast<uint8_t>(DescriptorSetIndex::FRAME_SET)) {
            continue;
        }
        if (binding.name[0] == '\0') {
            continue;
        }

        switch (binding.type) {
            case ShaderBindingType::UniformBuffer: {
                if (!shader_program->get_uniform_buffer_members(binding.name, members, buffer_size)
                    || buffer_size == 0) {
                    // Still register the binding even if member reflection failed.
                    add_binding_property(binding, PropertyKind::UniformBuffer);
                    break;
                }
                add_uniform_buffer_property(
                    binding.name,
                    buffer_size,
                    to_rhi_uniform_members(members),
                    true,
                    binding.binding,
                    binding.descriptor_set);
                break;
            }
            case ShaderBindingType::SampledImage:
            case ShaderBindingType::CombinedImageSampler:
            case ShaderBindingType::StorageImage:
                add_binding_property(binding, PropertyKind::Texture);
                break;
            case ShaderBindingType::Sampler:
                add_binding_property(binding, PropertyKind::Sampler);
                break;
            case ShaderBindingType::StorageBuffer:
                add_binding_property(binding, PropertyKind::StorageBuffer);
                break;
            default:
                break;
        }
    }
}

const Material::MaterialProperty* Material::find_material_property(uint64_t name_id) const noexcept {
    const auto it = material_property_indices_.find(name_id);
    if (it == material_property_indices_.end()) {
        return nullptr;
    }
    return &material_properties_[it->second];
}

Material::OwnedUniformBuffer* Material::find_owned_uniform_buffer(uint64_t name_id) noexcept {
    const auto it = uniform_buffers_.find(name_id);
    if (it == uniform_buffers_.end()) {
        return nullptr;
    }
    return &it->second;
}

DescriptorSet* Material::find_descriptor_set_by_property_name(uint64_t name_id) const noexcept {
    (void)name_id;
    return material_descriptor_set_;
}

void Material::resolve_material_descriptor_layout() {
    if (material_descriptor_set_layout_ != nullptr) {
        return;
    }

    for (size_t set_index = 0; set_index < descriptor_set_layouts_.size(); ++set_index) {
        DescriptorSetLayout* layout = descriptor_set_layouts_[set_index];
        if (layout == nullptr) {
            continue;
        }
        material_descriptor_set_index_ = static_cast<uint32_t>(set_index);
        material_descriptor_set_layout_ = layout;
        return;
    }
}

void Material::create_material_descriptor_set() {
    if (material_descriptor_set_ != nullptr) {
        return;
    }

    if (material_descriptor_set_layout_ == nullptr) {
        resolve_material_descriptor_layout();
    }

    if (material_descriptor_set_layout_ == nullptr) {
        return;
    }

    material_descriptor_set_ = material_descriptor_set_layout_->allocate_descriptor_set();
}

void Material::try_finish_gpu_init() {
    if (material_descriptor_set_ == nullptr) {
        descriptor_set_layouts_.fill(nullptr);
        collect_non_global_descriptor_set_layouts(shader_program_, descriptor_set_layouts_);
        material_descriptor_set_layout_ = nullptr;
        resolve_material_descriptor_layout();
        create_material_descriptor_set();
    }

    ensure_uniform_buffer_gpus();
}

bool Material::is_material_infrastructure_ready() const noexcept {
    if (requires_local_descriptor_set_layout() && material_descriptor_set_ == nullptr) {
        return false;
    }

    if (has_material_uniform_buffer()) {
        for (const auto& [name_id, ubo] : uniform_buffers_) {
            (void)name_id;
            if (ubo.size > 0 && ubo.buffer.handle() == 0) {
                return false;
            }
        }
    }

    return true;
}

bool Material::is_GPU_ready() {
    try_finish_gpu_init();
    return is_material_infrastructure_ready();
}

void Material::set_property(uint64_t name_id, const void* data, size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }

    const MaterialProperty* property = find_material_property(name_id);
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
    memcpy(ubo->cpu_data.data() + property->offset, data, copy_size);
    ubo->dirty = true;
    queue_uniform_buffer_update();
}

void Material::set_property(uint64_t name_id, const TextureHandle& texture) {
    if (name_id == 0 || texture.texture_ == nullptr) {
        return;
    }

    const MaterialProperty* property = find_material_property(name_id);
    if (property != nullptr && property->kind != PropertyKind::Texture) {
        return;
    }

    texture_handles_[name_id] = texture;
    FrameResources::instance().queue_material_update(
        MaterialUpdateRequest::make_texture(this, name_id, texture));
}

void Material::set_bindless_texture(uint64_t name_id, const TextureHandle& texture) {
    if (name_id == 0 || texture.texture_ == nullptr) {
        return;
    }
    texture_handles_[name_id] = texture;
}

bool Material::is_renderable() {
    if (!is_GPU_ready()) {
        return false;
    }

    for (const auto& [name_id, handle] : texture_handles_) {
        (void)name_id;
        Texture* bound_texture = handle.texture_;
        if (bound_texture == nullptr ||
            bound_texture->gpu_resource_state() < GPUResourceState::GPU_Visible) {
            return false;
        }
    }
    return true;
}

bool Material::requires_local_descriptor_set_layout() const noexcept {
    for (DescriptorSetLayout* layout : descriptor_set_layouts_) {
        if (layout != nullptr) {
            return true;
        }
    }

    if (shader_program_ != nullptr) {
        for (DescriptorSetLayout* layout : shader_program_->descriptor_set_layouts()) {
            if (is_material_local_descriptor_set_layout(shader_program_, layout)) {
                return true;
            }
        }
    }
    return false;
}

void Material::ensure_uniform_buffer_gpus() {
    if (device_ == nullptr || material_descriptor_set_ == nullptr) {
        return;
    }

    DescriptorSetLayout* layout = material_descriptor_set_layout();
    if (layout == nullptr || !layout->has_uniform_buffer_binding()) {
        return;
    }

    for (auto& [name_id, ubo] : uniform_buffers_) {
        if (ubo.buffer.handle() != 0 || ubo.size == 0) {
            continue;
        }

        const MaterialProperty* property = find_material_property(name_id);
        const char* buffer_name = property != nullptr ? property->name.c_str() : "material_uniform";
        ubo.buffer = ResourceManager::instance().create_buffer<std::byte>(
            device_,
            ubo.size,
            GraphicBufferBindFlags::ConstantBuffer,
            buffer_name);
        ubo.descriptor_bound = false;
        ubo.dirty = true;
    }
}

void Material::upload_owned_uniform_buffer(uint64_t name_id, OwnedUniformBuffer& ubo) {
    if (ubo.buffer.handle() == 0 || ubo.size == 0) {
        return;
    }

    DescriptorSet* descriptor_set = find_descriptor_set_by_property_name(name_id);
    if (descriptor_set == nullptr) {
        return;
    }

    ubo.buffer.copy_from_immediately(ubo.cpu_data.data(), ubo.size);

    if (!ubo.descriptor_bound) {
        descriptor_set->update_buffer(
            name_id,
            ubo.buffer.handle(),
            0,
            ubo.size);
        ubo.descriptor_bound = true;
    }
    ubo.dirty = false;
}

void Material::queue_uniform_buffer_update() {
    if (in_update_queue_) {
        return;
    }
    in_update_queue_ = true;
    FrameResources::instance().queue_material_update(
        MaterialUpdateRequest::make_uniform_buffer(this));
}

void Material::apply_material_parameters_upload() {
    ensure_uniform_buffer_gpus();

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

void Material::add_sampler(uint64_t name_id, const TextureSampler& sampler) {
    const MaterialProperty* property = find_material_property(name_id);
    if (property != nullptr && property->kind != PropertyKind::Sampler) {
        return;
    }

    FrameResources::instance().queue_material_update(
        MaterialUpdateRequest::make_sampler(this, name_id, sampler));
}

}// namespace ocarina

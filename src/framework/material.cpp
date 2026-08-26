#include "material.h"
#include "core/hash.h"
#include "core/logging.h"
#include "entity_component_system.h"
#include "resource_manager.h"
#include "rhi/descriptor_set.h"
#include "rhi/device.h"
#include "rhi/pipeline_state.h"
#include "rhi/shader_base.h"
#include "rhi/resources/resource.h"
#include "framework/frame_resources.h"
#include <algorithm>

namespace ocarina {
namespace {

void collect_non_global_descriptor_set_layouts(
    const RHIShader* shader,
    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& out_layouts) {
    if (shader == nullptr) {
        return;
    }
    for (DescriptorSetLayout* layout : shader->descriptor_set_layouts()) {
        if (layout == nullptr || FrameResources::is_global_singleton_layout(layout)) {
            continue;
        }
        const uint32_t set_index = layout->get_descriptor_set_index();
        if (set_index < out_layouts.size()) {
            out_layouts[set_index] = layout;
        }
    }
}

}// namespace

Material::Material(Device* device, handle_ty vertex_shader, handle_ty pixel_shader) : device_(device) {
    pipeline_state_ = PipelineState::MakeGraphicsDefault(vertex_shader, pixel_shader);

    const RHIShader* vertex = reinterpret_cast<const RHIShader*>(vertex_shader);
    const RHIShader* pixel = reinterpret_cast<const RHIShader*>(pixel_shader);
    uses_global_material_buffer_ = detect_global_material_buffer(pixel);

    // Layouts come from shaders (created after reflection), not from RHIPipelineLayout /
    // async PSO compile — so local sets are available as soon as shaders exist.
    collect_non_global_descriptor_set_layouts(vertex, descriptor_set_layouts_);
    collect_non_global_descriptor_set_layouts(pixel, descriptor_set_layouts_);

    create_material_descriptor_set();
    init_material_properties(pixel_shader);
    ensure_uniform_buffer_gpus();
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
    for (const RHIShader::UniformBufferMember& member : members) {
        MaterialProperty property;
        property.name = member.name;
        property.kind = PropertyKind::UniformMember;
        property.type = member.type;
        property.size = member.size;
        property.offset = member.offset;
        property.uniform_buffer_name_id = buffer_name_id;
        material_property_indices_.emplace(hash64(property.name), material_properties_.size());
        material_properties_.push_back(std::move(property));
    }
}

void Material::add_uniform_buffer_property(
    const char* binding_name,
    uint32_t buffer_size,
    const std::vector<RHIShader::UniformBufferMember>& members,
    bool create_owned_buffer) {
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
    ubo_property.size = buffer_size;
    ubo_property.uniform_buffer_name_id = name_id;
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

bool Material::detect_global_material_buffer(const RHIShader* pixel_shader) noexcept {
    return pixel_shader != nullptr
        && pixel_shader->has_descriptor_binding(kMaterialsBufferName);
}

void Material::init_material_properties(handle_ty pixel_shader) {
    const RHIShader* shader = reinterpret_cast<const RHIShader*>(pixel_shader);
    if (shader == nullptr) {
        return;
    }

    material_properties_.clear();
    material_property_indices_.clear();
    uniform_buffers_.clear();
    material_params_buffer_name_id_ = 0;
    material_params_byte_size_ = 0;

    std::vector<RHIShader::UniformBufferMember> members;
    uint32_t buffer_size = 0;
    bool found_local_ubo = false;

    // 1) All UBOs on non-global descriptor set layouts (per-material sets).
    //    Each UBO becomes a UniformBuffer property; its members become UniformMember properties.
    if (!uses_global_material_buffer_ && !uses_shared_bindless_descriptor_set_) {
        for (DescriptorSetLayout* layout : descriptor_set_layouts_) {
            if (layout == nullptr) {
                continue;
            }
            const size_t bindings_count = layout->get_bindings_count();
            for (size_t i = 0; i < bindings_count; ++i) {
                if (!layout->binding_is_uniform_buffer(i)) {
                    continue;
                }
                const char* binding_name = layout->get_binding_name(i);
                if (binding_name == nullptr || binding_name[0] == '\0') {
                    continue;
                }
                if (!shader->get_uniform_buffer_members(binding_name, members, buffer_size)) {
                    continue;
                }
                add_uniform_buffer_property(binding_name, buffer_size, members, true);
                found_local_ubo = true;
            }
        }
    }

    if (found_local_ubo) {
        return;
    }

    // 2) Global g_materials path: reflect MaterialParams members (CPU staging in ECS).
    if (uses_global_material_buffer_
        && shader->get_struct_members(kMaterialParamsStructName, members, buffer_size)) {
        material_params_buffer_name_id_ = hash64(kMaterialsBufferName);
        material_params_byte_size_ = buffer_size;
        apply_reflected_members(material_params_buffer_name_id_, buffer_size, members);
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
    if (name_id == 0) {
        return material_descriptor_set_;
    }

    if (DescriptorSet* set = FrameResources::instance().get_global_descriptor_set(name_id)) {
        return set;
    }

    const MaterialProperty* property = find_material_property(name_id);
    if (property != nullptr && property->uniform_buffer_name_id != 0
        && property->uniform_buffer_name_id != name_id) {
        if (DescriptorSet* set =
                FrameResources::instance().get_global_descriptor_set(property->uniform_buffer_name_id)) {
            return set;
        }
    }

    return material_descriptor_set_;
}

void Material::create_material_descriptor_set() {
    // Only allocate sets that are not process-wide globals (FRAME / SCENE / shared bindless).
    uint32_t material_set_index = InvalidUI32;
    DescriptorSetLayout* material_layout = nullptr;
    for (size_t set_index = 0; set_index < descriptor_set_layouts_.size(); ++set_index) {
        DescriptorSetLayout* layout = descriptor_set_layouts_[set_index];
        if (layout == nullptr) {
            continue;
        }
        material_set_index = static_cast<uint32_t>(set_index);
        material_layout = layout;
        break;
    }

    // Legacy material_ubo may live on the shared bindless MATERIAL_SET (FrameResources-owned).
    if (material_set_index == InvalidUI32 && !uses_global_material_buffer_) {
        const RHIShader* shaders[2] = {
            reinterpret_cast<const RHIShader*>(pipeline_state_.shaders[0]),
            reinterpret_cast<const RHIShader*>(pipeline_state_.shaders[1]),
        };
        for (const RHIShader* shader : shaders) {
            if (shader == nullptr) {
                continue;
            }
            for (DescriptorSetLayout* layout : shader->descriptor_set_layouts()) {
                if (layout == nullptr
                    || !layout->has_uniform_buffer_binding()
                    || !FrameResources::is_global_singleton_layout(layout)) {
                    continue;
                }
                material_descriptor_set_index_ = layout->get_descriptor_set_index();
                material_descriptor_set_ = nullptr;
                material_descriptor_set_layout_ = nullptr;
                uses_shared_bindless_descriptor_set_ = true;
                return;
            }
        }
    }

    if (material_set_index == InvalidUI32 || material_layout == nullptr) {
        return;
    }

    material_descriptor_set_index_ = material_set_index;
    material_descriptor_set_layout_ = material_layout;
    uses_shared_bindless_descriptor_set_ = false;
    material_descriptor_set_ = material_layout->allocate_descriptor_set();
}

void Material::set_property(uint64_t name_id, const void* data, size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }

    const MaterialProperty* property = find_material_property(name_id);
    if (property == nullptr || property->kind != PropertyKind::UniformMember) {
        return;
    }

    if (uses_global_material_buffer_) {
        ensure_material_buffer();
        if (!has_material_buffer()) {
            return;
        }

        EntityComponentSystem& ecs = EntityComponentSystem::instance();
        uint8_t* buffer_data =
            ecs.material_parameters_buffer().data() + material_buffer_offset_;
        const size_t copy_size = std::min(size, static_cast<size_t>(property->size));
        memcpy(buffer_data + property->offset, data, copy_size);
        queue_uniform_buffer_update();
        return;
    }

    // Resolve the descriptor set that owns this UBO binding by property / binding name.
    if (find_descriptor_set_by_property_name(
            property->uniform_buffer_name_id != 0 ? property->uniform_buffer_name_id : name_id)
        == nullptr
        && material_descriptor_set_ == nullptr) {
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
    if (name_id == 0 ||
        (texture.bindless_index_ == InvalidUI32 && texture.texture_ == nullptr)) {
        return;
    }

    textures_ready_.store(false, std::memory_order_relaxed);
    FrameResources::instance().queue_material_update(
        MaterialUpdateRequest::make_texture(this, name_id, texture));
}

Texture* Material::resolve_texture_handle(const TextureHandle& handle) noexcept {
    if (handle.texture_ != nullptr) {
        return handle.texture_;
    }
    if (handle.bindless_index_ == InvalidUI32) {
        return nullptr;
    }
    return BindlessTextureRegistry::instance().get_texture(handle.bindless_index_);
}

Texture* Material::bind_texture(uint64_t name_id, const TextureHandle& handle) {
    texture_handles_[name_id] = handle;
    if (uses_global_material_buffer_ && handle.bindless_index_ != InvalidUI32) {
        set_property(name_id, handle.bindless_index_);
    }

    Texture* texture = resolve_texture_handle(handle);
    if (texture != nullptr) {
        texture_handles_[name_id].texture_ = texture;
    }
    textures_ready_.store(false, std::memory_order_relaxed);
    return texture;
}

bool Material::evaluate_textures_ready() {
    bool all_ready = true;
    for (auto& [name_id, handle] : texture_handles_) {
        (void)name_id;
        Texture* texture = resolve_texture_handle(handle);
        if (texture == nullptr) {
            all_ready = false;
            continue;
        }
        handle.texture_ = texture;
        if (texture->gpu_resource_state() < GPUResourceState::GPU_Visible) {
            all_ready = false;
        }
    }
    return all_ready;
}

bool Material::is_renderable() {
    if (textures_ready_.load(std::memory_order_relaxed)) {
        return true;
    }
    textures_ready_.store(evaluate_textures_ready(), std::memory_order_relaxed);
    return textures_ready_.load(std::memory_order_relaxed);
}

void Material::ensure_uniform_buffer_gpus() {
    if (uses_global_material_buffer_ ||
        uses_shared_bindless_descriptor_set_ ||
        device_ == nullptr ||
        material_descriptor_set_ == nullptr) {
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
        const char* buffer_name = property != nullptr ? property->name.c_str() : "material_ubo";
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
    if (uses_global_material_buffer_) {
        return;
    }

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
    FrameResources::instance().queue_material_update(
        MaterialUpdateRequest::make_sampler(this, name_id, sampler));
}

void Material::ensure_material_buffer() {
    if (!uses_global_material_buffer_) {
        return;
    }
    if (material_buffer_offset_ != InvalidUI32) {
        return;
    }
    if (material_params_byte_size_ == 0) {
        return;
    }

    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    material_buffer_offset_ = ecs.allocate_material_buffer_region(material_params_byte_size_);
    material_buffer_size_ = material_params_byte_size_;
    memset(
        ecs.material_parameters_buffer().data() + material_buffer_offset_,
        0,
        material_params_byte_size_);
}

}// namespace ocarina

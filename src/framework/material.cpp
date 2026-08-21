#include "material.h"
#include "core/hash.h"
#include "entity_component_system.h"
#include "pipeline_manager.h"
#include "resource_manager.h"
#include "rhi/descriptor_set.h"
#include "rhi/device.h"
#include "rhi/shader_base.h"
#include "rhi/resources/resource.h"
#include "framework/frame_resources.h"
#include <algorithm>

namespace ocarina {

Material::Material(Device* device, handle_ty vertex_shader, handle_ty pixel_shader) : device_(device) {
    pipeline_state_ = PipelineState::MakeGraphicsDefault(vertex_shader, pixel_shader);

    // Descriptor set layouts / pipeline layout are created in PipelineCompileTask /
    // PipelineManager, which also registers FRAME/SCENE/shared-bindless sets.
    if (RHIPipelineLayout* pipeline_layout =
            PipelineManager::instance().get_pipeline_layout(pipeline_state_.shaders)) {
        descriptor_set_layouts_ = pipeline_layout->descriptor_set_layouts_;
    }

    uses_global_material_buffer_ = detect_global_material_buffer_layout();
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

bool Material::detect_global_material_buffer_layout() const noexcept {
    static const uint64_t kGMaterials = hash64(kMaterialsBufferName);
    for (DescriptorSetLayout* layout : descriptor_set_layouts_) {
        if (layout == nullptr || !FrameResources::is_global_singleton_layout(layout)) {
            continue;
        }
        const size_t bindings_count = layout->get_bindings_count();
        for (size_t i = 0; i < bindings_count; ++i) {
            if (layout->get_binding_name_id(i) == kGMaterials) {
                return true;
            }
        }
    }
    return false;
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
            if (layout == nullptr || FrameResources::is_global_singleton_layout(layout)) {
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

void Material::create_material_descriptor_set() {
    // Only allocate sets that are not process-wide globals (FRAME / SCENE / shared bindless).
    // Those are created during pipeline layout registration in PipelineManager.
    uint32_t material_set_index = InvalidUI32;
    for (size_t set_index = 0; set_index < descriptor_set_layouts_.size(); ++set_index) {
        DescriptorSetLayout* layout = descriptor_set_layouts_[set_index];
        if (layout == nullptr) {
            continue;
        }
        if (FrameResources::is_global_singleton_layout(layout)) {
            continue;
        }
        material_set_index = static_cast<uint32_t>(set_index);
        break;
    }

    // Legacy material_ubo may live on the shared bindless MATERIAL_SET.
    if (material_set_index == InvalidUI32 && !uses_global_material_buffer_) {
        for (size_t set_index = 0; set_index < descriptor_set_layouts_.size(); ++set_index) {
            DescriptorSetLayout* layout = descriptor_set_layouts_[set_index];
            if (layout == nullptr || !layout->has_uniform_buffer_binding()) {
                continue;
            }
            if (FrameResources::is_global_singleton_layout(layout)) {
                material_descriptor_set_index_ = layout->get_descriptor_set_index();
                material_descriptor_set_ =
                    FrameResources::instance().get_descriptor_set(material_descriptor_set_index_);
                uses_shared_bindless_descriptor_set_ = material_descriptor_set_ != nullptr;
                return;
            }
        }
    }

    if (material_set_index == InvalidUI32) {
        return;
    }

    DescriptorSetLayout* material_layout = descriptor_set_layouts_[material_set_index];
    if (material_layout == nullptr) {
        return;
    }

    material_descriptor_set_index_ = material_set_index;
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
        try_queue_uniform_buffer_update();
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

void Material::bind_texture(uint64_t name_id, const TextureHandle& handle) {
    texture_handles_[name_id] = handle;
    if (uses_global_material_buffer_ && handle.bindless_index_ != InvalidUI32) {
        set_property(name_id, handle.bindless_index_);
    }

    Texture* texture = resolve_texture_handle(handle);
    if (texture != nullptr) {
        texture_handles_[name_id].texture_ = texture;
        if (!uses_global_material_buffer_ &&
            !uses_shared_bindless_descriptor_set_ &&
            material_descriptor_set_ != nullptr) {
            material_descriptor_set_->update_texture(name_id, texture);
        }
    }
    textures_ready_.store(false, std::memory_order_relaxed);
}

bool Material::evaluate_textures_ready() {
    bool all_ready = true;
    for (auto& [name_id, handle] : texture_handles_) {
        Texture* texture = resolve_texture_handle(handle);
        if (texture == nullptr) {
            all_ready = false;
            continue;
        }
        handle.texture_ = texture;
        if (!texture->is_gpu_ready()) {
            all_ready = false;
            continue;
        }

        if (!uses_global_material_buffer_ &&
            !uses_shared_bindless_descriptor_set_ &&
            material_descriptor_set_ != nullptr) {
            material_descriptor_set_->update_texture(name_id, texture);
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
    if (material_descriptor_set_ == nullptr || ubo.buffer.handle() == 0 || ubo.size == 0) {
        return;
    }

    ubo.buffer.copy_from_immediately(ubo.cpu_data.data(), ubo.size);

    if (!ubo.descriptor_bound) {
        material_descriptor_set_->update_buffer(
            name_id,
            ubo.buffer.handle(),
            0,
            ubo.size);
        ubo.descriptor_bound = true;
    }
    ubo.dirty = false;
}

void Material::try_queue_uniform_buffer_update() {
    if (in_update_queue_) {
        return;
    }
    in_update_queue_ = true;
    FrameResources::instance().queue_material_update(
        MaterialUpdateRequest::make_uniform_buffer(this));
}

void Material::queue_uniform_buffer_update() {
    try_queue_uniform_buffer_update();
}

void Material::apply_material_parameters_upload() {
    if (uses_global_material_buffer_) {
        // Global `g_materials` uploads are handled directly in FrameResources.
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
    if (material_descriptor_set_ == nullptr) {
        return;
    }

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

#include "material.h"
#include "core/hash.h"
#include "bindless_texture_registry.h"
#include "entity_component_system.h"
#include "rhi/descriptor_set.h"
#include "rhi/device.h"
#include "rhi/shader_base.h"
#include "framework/frame_resources.h"
#include <algorithm>

namespace ocarina {

static std::vector<uint64_t> collect_binding_name_ids(DescriptorSetLayout* layout) {
    std::vector<uint64_t> binding_name_ids;
    if (layout == nullptr) {
        return binding_name_ids;
    }
    const size_t bindings_count = layout->get_bindings_count();
    binding_name_ids.reserve(bindings_count);
    for (size_t i = 0; i < bindings_count; ++i) {
        binding_name_ids.push_back(layout->get_binding_name_id(i));
    }
    return binding_name_ids;
}

uint32_t Material::find_bindless_descriptor_set_index(
    const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts) {
    for (size_t set_index = 0; set_index < layouts.size(); ++set_index) {
        DescriptorSetLayout* layout = layouts[set_index];
        if (layout != nullptr && layout->has_bindless_binding()) {
            return layout->get_descriptor_set_index();
        }
    }
    return InvalidUI32;
}

uint32_t Material::find_global_ubo_descriptor_set_index(
    const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts) {
    for (size_t set_index = 0; set_index < layouts.size(); ++set_index) {
        DescriptorSetLayout* layout = layouts[set_index];
        if (layout == nullptr) {
            continue;
        }
        if (layout->get_descriptor_set_index() != static_cast<uint32_t>(DescriptorSetIndex::GLOBAL_SET)) {
            continue;
        }
        if (!layout->has_bindless_binding() && layout->has_uniform_buffer_binding()) {
            return layout->get_descriptor_set_index();
        }
    }
    return InvalidUI32;
}

uint32_t Material::find_material_ubo_descriptor_set_index(
    const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts) {
    for (size_t set_index = 0; set_index < layouts.size(); ++set_index) {
        DescriptorSetLayout* layout = layouts[set_index];
        if (layout == nullptr) {
            continue;
        }
        if (layout->has_bindless_binding()) {
            continue;
        }
        if (!layout->has_uniform_buffer_binding()) {
            continue;
        }
        if (layout->get_descriptor_set_index() == static_cast<uint32_t>(DescriptorSetIndex::GLOBAL_SET)) {
            continue;
        }
        return layout->get_descriptor_set_index();
    }
    return InvalidUI32;
}

uint32_t Material::find_max_descriptor_set_index(
    const std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER>& layouts) {
    uint32_t max_index = 0;
    bool any = false;
    for (size_t set_index = 0; set_index < layouts.size(); ++set_index) {
        if (layouts[set_index] != nullptr) {
            max_index = static_cast<uint32_t>(set_index);
            any = true;
        }
    }
    return any ? max_index : InvalidUI32;
}

Material::Material(Device* device, handle_ty vertex_shader, handle_ty pixel_shader) : device_(device) {
    pipeline_state_.shaders[0] = vertex_shader;
    pipeline_state_.shaders[1] = pixel_shader;
    pipeline_state_.descriptorset_layout = InvalidUI64;
    pipeline_state_.raster_state = RasterState::Default();
    pipeline_state_.blend_state = BlendState::Opaque();
    pipeline_state_.depth_stencil_state = DepthStencilState::Default();
    pipeline_state_.primitive_type = PrimitiveType::TRIANGLES;

    void* shaders[2] = {
        reinterpret_cast<void*>(vertex_shader),
        reinterpret_cast<void*>(pixel_shader)
    };
    descriptor_set_layouts_ = device_->create_descriptor_set_layout(shaders, 2);
    init_material_properties(pixel_shader);
    create_global_descriptor_sets();
    create_material_descriptor_set();
}

void Material::init_material_properties(handle_ty pixel_shader) {
    const RHIShader* shader = reinterpret_cast<const RHIShader*>(pixel_shader);
    if (shader == nullptr) {
        return;
    }

    std::vector<RHIShader::UniformBufferMember> members;
    uint32_t buffer_size = 0;
    if (!shader->get_uniform_buffer_members(kMaterialUniformBufferName, members, buffer_size)) {
        return;
    }

    material_uniform_buffer_name_id_ = hash64(kMaterialUniformBufferName);
    material_uniform_buffer_size_ = buffer_size;
    material_properties_.reserve(members.size());
    material_property_indices_.clear();

    for (const RHIShader::UniformBufferMember& member : members) {
        MaterialProperty property;
        property.name = member.name;
        property.type = member.type;
        property.size = member.size;
        property.offset = member.offset;
        material_property_indices_.emplace(hash64(property.name), material_properties_.size());
        material_properties_.push_back(std::move(property));
    }
}

const Material::MaterialProperty* Material::find_material_property(uint64_t name_id) const noexcept {
    const auto it = material_property_indices_.find(name_id);
    if (it == material_property_indices_.end()) {
        return nullptr;
    }
    return &material_properties_[it->second];
}

void Material::create_global_descriptor_sets() {
    FrameResources& frame_resources = FrameResources::instance();

    const uint32_t global_ubo_set_index = find_global_ubo_descriptor_set_index(descriptor_set_layouts_);
    if (global_ubo_set_index != InvalidUI32) {
        DescriptorSetLayout* global_layout = descriptor_set_layouts_[global_ubo_set_index];
        std::vector<uint64_t> binding_name_ids = collect_binding_name_ids(global_layout);
        if (!binding_name_ids.empty()) {
            frame_resources.get_or_create_global_descriptor_set(
                global_ubo_set_index,
                binding_name_ids,
                [global_layout]() { return global_layout->allocate_descriptor_set(); });
        }
    }

    const uint32_t bindless_set_index = find_bindless_descriptor_set_index(descriptor_set_layouts_);
    if (bindless_set_index != InvalidUI32) {
        DescriptorSetLayout* bindless_layout = descriptor_set_layouts_[bindless_set_index];
        std::vector<uint64_t> binding_name_ids = collect_binding_name_ids(bindless_layout);
        if (!binding_name_ids.empty()) {
            frame_resources.get_or_create_bindless_descriptor_set(
                bindless_set_index,
                binding_name_ids,
                [bindless_layout]() { return bindless_layout->allocate_descriptor_set(); });
        }
    }
}

void Material::create_material_descriptor_set() {
    uint32_t material_set_index = find_material_ubo_descriptor_set_index(descriptor_set_layouts_);
    if (material_set_index == InvalidUI32) {
        FrameResources& frame_resources = FrameResources::instance();
        for (size_t set_index = 0; set_index < descriptor_set_layouts_.size(); ++set_index) {
            if (frame_resources.is_global_descriptor_set_index(static_cast<uint32_t>(set_index))) {
                continue;
            }
            if (descriptor_set_layouts_[set_index] == nullptr) {
                continue;
            }
            if (descriptor_set_layouts_[set_index]->has_bindless_binding()) {
                continue;
            }
            material_set_index = static_cast<uint32_t>(set_index);
            break;
        }
    }
    if (material_set_index == InvalidUI32) {
        return;
    }

    DescriptorSetLayout* material_layout = descriptor_set_layouts_[material_set_index];
    if (material_layout == nullptr) {
        return;
    }

    material_descriptor_set_ = material_layout->allocate_descriptor_set();
    material_descriptor_set_index_ = material_set_index;
}

void Material::upload_material_uniform_buffer(const void* data, uint32_t size) {
    if (material_descriptor_set_ == nullptr || data == nullptr || size == 0) {
        return;
    }

    const uint32_t upload_size = std::min(size, material_uniform_buffer_size_);
    material_descriptor_set_->update_buffer(
        material_uniform_buffer_name_id_,
        data,
        upload_size);
}

void Material::add_bindless_texture(uint64_t name_id, Texture* texture) {
    const uint32_t bindless_index = BindlessTextureRegistry::instance().allocate_index(texture);
    FrameResources::instance().update_bindless_texture_at_index(bindless_index, texture);
    bindless_textures_.insert_or_assign(name_id, TextureHandle{bindless_index, texture});
}

Material::TextureHandle Material::get_bindless_texture_handle(uint64_t name_id) const {
    const auto it = bindless_textures_.find(name_id);
    if (it != bindless_textures_.end()) {
        return it->second;
    }
    return TextureHandle{InvalidUI32, nullptr};
}

void Material::add_texture(uint64_t name_id, Texture* texture) {
    if (material_descriptor_set_ != nullptr) {
        material_descriptor_set_->update_texture(name_id, texture);
    }
}

void Material::add_sampler(uint64_t name_id, const TextureSampler& sampler) {
    if (material_descriptor_set_ != nullptr) {
        material_descriptor_set_->update_sampler(name_id, sampler);
    }
}

void Material::ensure_material_buffer() {
    if (!has_material_uniform_buffer() || material_buffer_offset_ != InvalidUI32) {
        return;
    }

    const uint32_t buffer_size = material_uniform_buffer_size_;
    if (buffer_size == 0) {
        return;
    }

    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    material_buffer_offset_ = ecs.allocate_material_buffer_region(buffer_size);
    material_buffer_size_ = buffer_size;
    memset(
        ecs.material_parameters_buffer().data() + material_buffer_offset_,
        0,
        buffer_size);
    material_parameters_dirty_ = true;
}

}// namespace ocarina

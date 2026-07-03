//
// Created by Zero on 06/06/2022.
//

#include "primitive.h"
#include "pipeline_manager.h"
#include "material.h"
#include "core/hash.h"
#include "rhi/vertex_buffer.h"
#include "rhi/index_buffer.h"
#include "rhi/device.h"
#include "rhi/descriptor_set.h"
#include "rhi/resources/texture_sampler.h"
#include "mesh.h"
#include "bindless_texture_registry.h"
#include "frame_resources.h"

namespace ocarina {


Primitive::~Primitive() {
    if (push_constant_data_)
    {
        ocarina::deallocate(push_constant_data_);
        push_constant_data_ = nullptr;
    }
}

void Primitive::set_geometry_data_setup(Device *device, GeometryDataSetup setup) {
    geometry_data_setup_ = setup;
    if (geometry_data_setup_) {
        geometry_data_setup_(*this);
    }

    update_descriptor_sets(device);
}

void Primitive::add_descriptor_set(DescriptorSet *descriptor_set) {
    auto it = std::find(descriptor_sets_.begin(), descriptor_sets_.end(), descriptor_set);
    if (it == descriptor_sets_.end()) {
        descriptor_sets_.push_back(descriptor_set);
    }
}

void Primitive::set_material_parameter(uint64_t name_id, const void* data, size_t size) {
    if (material_ == nullptr || data == nullptr || size == 0) {
        return;
    }

    const Material::MaterialProperty* property = material_->find_material_property(name_id);
    if (property == nullptr) {
        return;
    }

    if (material_parameters_buffer_.size() != material_->material_uniform_buffer_size()) {
        material_parameters_buffer_.assign(material_->material_uniform_buffer_size(), 0);
    }

    const size_t copy_size = std::min(size, static_cast<size_t>(property->size));
    memcpy(material_parameters_buffer_.data() + property->offset, data, copy_size);
    material_parameters_dirty_ = true;
}

void Primitive::upload_material_parameters() {
    if (!material_parameters_dirty_ || material_ == nullptr || !material_->has_material_uniform_buffer()) {
        return;
    }
    if (material_parameters_buffer_.empty()) {
        return;
    }

    const uint64_t buffer_name_id = material_->material_uniform_buffer_name_id();
    const uint32_t buffer_size = material_->material_uniform_buffer_size();
    for (DescriptorSet* descriptor_set : descriptor_sets_) {
        descriptor_set->update_buffer(
            buffer_name_id,
            material_parameters_buffer_.data(),
            buffer_size);
    }
    material_parameters_dirty_ = false;
}

void Primitive::update_render_component(Device* device, RenderComponent& render_component, TransformComponent& transform) {
    if (update_push_constant_function_ != nullptr) {
        update_push_constant_function_(*this, transform);
    }

    render_component.geometry = {};
    render_component.descriptor_sets.clear();
    render_component.push_constant_data = nullptr;
    render_component.push_constant_size = 0;

    if (material_ == nullptr) {
        return;
    }

    if (descriptor_sets_dirty_ || descriptor_sets_material_ != material_) {
        update_descriptor_sets(device);
    }

    upload_material_parameters();

    if (mesh_ != nullptr) {
        render_component.geometry = mesh_->geometry_slice();
    }

    const PipelineState& pipeline_state = material_->get_pipeline_state();
    RHIPipelineLayout* pipeline_layout = PipelineManager::instance().get_or_create_pipeline_layout(
        pipeline_state.shaders);
    const uint32_t push_constant_size = pipeline_layout != nullptr ? pipeline_layout->push_constant_size : 0;
    if (push_constant_data_ == nullptr && push_constant_size > 0) {
        push_constant_data_ = ocarina::allocate(push_constant_size);
        memset(push_constant_data_, 0, push_constant_size);
    }

    render_component.push_constant_size = static_cast<uint8_t>(push_constant_size);
    render_component.push_constant_data = push_constant_data_;
    render_component.descriptor_sets = descriptor_sets_;
    render_component.first_set = first_descriptor_set_;
}

void Primitive::add_texture(uint64_t name_id, Texture *texture) {
    
    uint32_t bindless_index = InvalidUI32;
    for (auto &descriptor_set : descriptor_sets_) {
        descriptor_set->update_texture(name_id, texture);
    }
    TextureHandle texture_handle{bindless_index, texture};
    textures_.insert(std::make_pair(name_id, texture_handle));
    descriptor_sets_dirty_ = true;
}

Primitive::TextureHandle Primitive::get_texture_handle(uint64_t name_id) const {
    auto it = bindless_textures_.find(name_id);
    if (it != bindless_textures_.end()) {
        return it->second;
    }
    it = textures_.find(name_id);
    if (it != textures_.end()) {
        return it->second;
    }
    return TextureHandle{InvalidUI32, nullptr};
}

void Primitive::add_bindless_texture(uint64_t name_id, Texture *texture) {
    const uint32_t bindless_index = BindlessTextureRegistry::instance().allocate_index(texture);
    FrameResources::instance().update_bindless_texture_at_index(bindless_index, texture);
    bindless_textures_.insert_or_assign(name_id, TextureHandle{bindless_index, texture});
}

void Primitive::add_sampler(uint64_t name_id, const TextureSampler& sampler)
{
    samplers_.insert(std::make_pair(name_id, sampler));
    descriptor_sets_dirty_ = true;
}

void Primitive::update_descriptor_sets(Device *device) {
    if (material_ == nullptr) {
        return;
    }

    for (auto &descriptor_set : descriptor_sets_) {
        ocarina::delete_with_allocator<DescriptorSet>(descriptor_set);
    }
    descriptor_sets_.clear();
    first_descriptor_set_ = 0;
    bool first_set_assigned = false;
    
    const auto& descriptor_set_layouts = material_->descriptor_set_layouts();
    for (size_t i = 0; i < MAX_DESCRIPTOR_SETS_PER_SHADER; ++i) {
        if (FrameResources::instance().is_global_descriptor_set_index(static_cast<uint32_t>(i))) {
            continue;
        }
        if (descriptor_set_layouts[i] == nullptr) {
            continue;
        }
        DescriptorSet* descriptor_set = descriptor_set_layouts[i]->allocate_descriptor_set();
        add_descriptor_set(descriptor_set);
        if (!first_set_assigned) {
            first_descriptor_set_ = static_cast<uint32_t>(i);
            first_set_assigned = true;
        }
    }

    for (auto &descriptor_set : descriptor_sets_) {
        for (auto &tex_pair : textures_) {
            descriptor_set->update_texture(tex_pair.first, tex_pair.second.texture_);
        }
        for (auto& sampler_pair : samplers_) {
            descriptor_set->update_sampler(sampler_pair.first, sampler_pair.second);
        }
    }
    upload_material_parameters();
    descriptor_sets_material_ = material_;
    descriptor_sets_dirty_ = false;
}

void Primitive::set_push_constant_variable(uint64_t name_id, const std::byte *data, size_t size) {
    if (push_constant_data_ == nullptr || material_ == nullptr) {
        return;
    }

    const PipelineState& pipeline_state = material_->get_pipeline_state();
    RHIPipelineLayout* pipeline_layout = PipelineManager::instance().get_or_create_pipeline_layout(
        pipeline_state.shaders);
    if (pipeline_layout == nullptr) {
        return;
    }

    const auto it = pipeline_layout->push_constant_variables_.find(name_id);
    if (it != pipeline_layout->push_constant_variables_.end()) {
        memcpy(push_constant_data_ + it->second.offset, data, size);
    }
}
}// namespace ocarina

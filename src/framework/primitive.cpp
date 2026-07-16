//
// Created by Zero on 06/06/2022.
//

#include "primitive.h"
#include "pipeline_manager.h"
#include "material.h"
#include "entity_component_system.h"
#include "core/hash.h"
#include "rhi/descriptor_set.h"
#include "mesh.h"
#include "transform_component.h"

namespace ocarina {

void Primitive::sync_render_component_material_buffer(RenderComponent& render_component) {
    if (material_ == nullptr || !material_->has_material_buffer()) {
        render_component.material_buffer_offset = InvalidUI32;
        render_component.material_buffer_size = 0;
        return;
    }
    render_component.material_buffer_offset = material_->material_buffer_offset();
    render_component.material_buffer_size = material_->material_buffer_size();
}

void Primitive::set_material(Material* material) {
    if (material_ == material) {
        return;
    }
    material_ = material;
    render_component_initialized_ = false;
    last_push_constant_transform_version_ = InvalidUI32;

    if (entity_index_ != InvalidUI32) {
        sync_render_component_material_buffer(
            EntityComponentSystem::instance().render_component(entity_index_));
    }
}

Primitive::~Primitive() {
    if (push_constant_data_) {
        ocarina::deallocate(push_constant_data_);
        push_constant_data_ = nullptr;
    }
}

void Primitive::set_geometry_data_setup(Device* device, GeometryDataSetup setup) {
    geometry_data_setup_ = setup;
    if (geometry_data_setup_) {
        geometry_data_setup_(*this);
    }
    upload_material_parameters(true);
}

void Primitive::set_material_parameter(uint64_t name_id, const void* data, size_t size) {
    if (data == nullptr || size == 0 || material_ == nullptr) {
        return;
    }

    const Material::MaterialProperty* property = material_->find_material_property(name_id);
    if (property == nullptr) {
        return;
    }

    material_->ensure_material_buffer();
    if (!material_->has_material_buffer()) {
        return;
    }

    EntityComponentSystem& ecs = EntityComponentSystem::instance();
    uint8_t* buffer_data =
        ecs.material_parameters_buffer().data() + material_->material_buffer_offset();
    const size_t copy_size = std::min(size, static_cast<size_t>(property->size));
    memcpy(buffer_data + property->offset, data, copy_size);
    material_->mark_material_parameters_dirty();

    if (entity_index_ != InvalidUI32) {
        sync_render_component_material_buffer(
            EntityComponentSystem::instance().render_component(entity_index_));
    }
}

void Primitive::upload_material_parameters(bool force_upload) {
    if (material_ == nullptr || !material_->has_material_uniform_buffer()) {
        return;
    }
    if (!material_->has_material_buffer()) {
        return;
    }
    if (!material_->material_parameters_dirty() && !force_upload) {
        return;
    }

    const uint64_t buffer_name_id = material_->material_uniform_buffer_name_id();
    const uint32_t buffer_size = material_->material_buffer_size();
    const uint8_t* buffer_data =
        EntityComponentSystem::instance().material_parameters_buffer().data()
        + material_->material_buffer_offset();

    if (material_->has_material_descriptor_set()) {
        material_->get_material_descriptor_set()->update_buffer(
            buffer_name_id,
            buffer_data,
            buffer_size);
    }
    material_->clear_material_parameters_dirty();
}

void Primitive::initialize_render_component(
    Device* device,
    RenderComponent& render_component,
    TransformComponent& transform) {
    if (render_component_initialized_) {
        return;
    }

    render_component.geometry = {};
    render_component.push_constant_data = nullptr;
    render_component.push_constant_size = 0;
    render_component.material_buffer_offset = InvalidUI32;
    render_component.material_buffer_size = 0;

    if (material_ == nullptr) {
        return;
    }

    sync_render_component_material_buffer(render_component);
    upload_material_parameters(true);

    if (mesh_ != nullptr) {
        render_component.geometry = mesh_->geometry_slice();
    }

    const PipelineState& pipeline_state = material_->get_pipeline_state();
    RHIPipelineLayout* pipeline_layout = PipelineManager::instance().get_pipeline_layout(
        pipeline_state.shaders);
    const uint32_t push_constant_size = pipeline_layout != nullptr ? pipeline_layout->push_constant_size : 0;
    if (push_constant_data_ == nullptr && push_constant_size > 0) {
        push_constant_data_ = ocarina::allocate(push_constant_size);
        memset(push_constant_data_, 0, push_constant_size);
    }

    update_push_constants(transform);

    render_component.push_constant_size = push_constant_size;
    render_component.push_constant_data = push_constant_data_;
    render_component_initialized_ = true;
}

void Primitive::update_push_constants(TransformComponent& transform) {
    if (update_push_constant_function_ == nullptr) {
        return;
    }
    if (render_component_initialized_
        && transform.transform_version() == last_push_constant_transform_version_) {
        return;
    }

    update_push_constant_function_(*this, transform);
    last_push_constant_transform_version_ = transform.transform_version();
}

void Primitive::update_render_component(
    Device* device,
    RenderComponent& render_component,
    TransformComponent& transform) {
    initialize_render_component(device, render_component, transform);
    update_push_constants(transform);
}

void Primitive::set_push_constant_variable(uint64_t name_id, const std::byte* data, size_t size) {
    if (push_constant_data_ == nullptr || material_ == nullptr) {
        return;
    }

    const PipelineState& pipeline_state = material_->get_pipeline_state();
    RHIPipelineLayout* pipeline_layout = PipelineManager::instance().get_pipeline_layout(
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

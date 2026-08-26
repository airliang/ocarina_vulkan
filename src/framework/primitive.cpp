//
// Created by Zero on 06/06/2022.
//

#include "primitive.h"
#include "material.h"
#include "entity_component_system.h"
#include "core/hash.h"
#include "mesh.h"
#include "transform_component.h"
#include "rhi/shader_base.h"
#include <algorithm>
#include <cstring>

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

RenderComponent* Primitive::current_render_component() noexcept {
    if (entity_index_ == InvalidUI32) {
        return nullptr;
    }
    return &EntityComponentSystem::instance().render_component(entity_index_);
}

void Primitive::ensure_push_constants_from_shaders(RenderComponent& render_component) {
    if (!render_component.push_constants.empty() || material_ == nullptr) {
        return;
    }

    const PipelineState& pipeline_state = material_->get_pipeline_state();
    const RHIShader* vertex = reinterpret_cast<const RHIShader*>(pipeline_state.shaders[0]);
    const RHIShader* pixel = reinterpret_cast<const RHIShader*>(pipeline_state.shaders[1]);
    if (vertex != nullptr) {
        vertex->collect_push_constant_ranges(render_component.push_constants);
    }
    if (pixel != nullptr) {
        pixel->collect_push_constant_ranges(render_component.push_constants);
    }
}

void Primitive::set_material(Material* material) {
    if (material_ == material) {
        return;
    }
    material_ = material;
    render_component_initialized_ = false;
    last_push_constant_transform_version_ = InvalidUI32;

    if (entity_index_ != InvalidUI32) {
        RenderComponent& render_component =
            EntityComponentSystem::instance().render_component(entity_index_);
        render_component.push_constants.clear();
        sync_render_component_material_buffer(render_component);
    }
}

void Primitive::set_geometry_data_setup(Device* device, GeometryDataSetup setup) {
    geometry_data_setup_ = setup;
    if (geometry_data_setup_) {
        geometry_data_setup_(*this);
    }
}

void Primitive::initialize_render_component(
    Device* device,
    RenderComponent& render_component,
    TransformComponent& transform) {
    (void)device;
    if (render_component_initialized_) {
        return;
    }

    render_component.mesh_id = InvalidUI32;
    render_component.push_constants.clear();
    render_component.material_buffer_offset = InvalidUI32;
    render_component.material_buffer_size = 0;

    if (material_ == nullptr) {
        return;
    }

    sync_render_component_material_buffer(render_component);

    if (mesh_ != nullptr) {
        render_component.mesh_id = mesh_->mesh_id();
    }

    // Push-constant ranges come from shader reflection — no RHIPipelineLayout required.
    ensure_push_constants_from_shaders(render_component);
    update_push_constants(transform);

    render_component_initialized_ = true;
}

void Primitive::write_ssbo_index_push_constants() {
    if (entity_index_ != InvalidUI32) {
        set_push_constant_variable(
            hash64("transform_index"),
            reinterpret_cast<const std::byte*>(&entity_index_),
            sizeof(entity_index_));
    }

    if (material_ != nullptr && material_->has_material_buffer()) {
        const uint32_t material_index = material_->material_slot_index();
        if (material_index != InvalidUI32) {
            set_push_constant_variable(
                hash64("material_index"),
                reinterpret_cast<const std::byte*>(&material_index),
                sizeof(material_index));
        }
    }
}

void Primitive::update_push_constants(TransformComponent& transform) {
    if (RenderComponent* render_component = current_render_component()) {
        ensure_push_constants_from_shaders(*render_component);
    }

    write_ssbo_index_push_constants();

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
    if (data == nullptr || size == 0) {
        return;
    }

    RenderComponent* render_component = current_render_component();
    if (render_component == nullptr) {
        return;
    }

    ensure_push_constants_from_shaders(*render_component);

    for (PushConstantRange& range : render_component->push_constants) {
        const auto it = range.variables.find(name_id);
        if (it == range.variables.end()) {
            continue;
        }

        const size_t copy_size = std::min(size, it->second.size);
        if (it->second.offset + copy_size > range.size
            || it->second.offset + copy_size > PushConstantRange::kMaxDataBytes) {
            return;
        }
        std::memcpy(range.data.data() + it->second.offset, data, copy_size);
        return;
    }
}

}// namespace ocarina

#include "render_component.h"
#include "material.h"
#include "mesh.h"
#include "resource_manager.h"
#include "transform_component.h"
#include "rhi/shader_base.h"
#include "rhi/shader_program.h"
#include "core/hash.h"

#include <algorithm>
#include <cstring>

namespace ocarina {

void RenderComponent::set_mesh(Mesh* mesh) {
    mesh_id = mesh != nullptr ? mesh->mesh_id() : InvalidUI32;
    initialized = false;
    last_push_constant_transform_version = InvalidUI32;
}

void RenderComponent::set_material(Material* new_material) {
    if (material == new_material) {
        return;
    }
    material = new_material;
    initialized = false;
    last_push_constant_transform_version = InvalidUI32;
    push_constants.clear();
}

Mesh* RenderComponent::get_mesh() const noexcept {
    return ResourceManager::instance().get_mesh(mesh_id);
}

void RenderComponent::ensure_push_constants_from_shaders() {
    if (!push_constants.empty() || material == nullptr) {
        return;
    }

    ShaderProgram* program = material->get_shader_program();
    if (program == nullptr) {
        return;
    }
    const RHIShader* vertex = program->vertex_shader();
    const RHIShader* pixel = program->pixel_shader();
    if (vertex != nullptr) {
        vertex->collect_push_constant_ranges(push_constants);
    }
    if (pixel != nullptr) {
        pixel->collect_push_constant_ranges(push_constants);
    }
}

void RenderComponent::set_push_constant_variable(
    uint64_t name_id,
    const std::byte* data,
    size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }

    ensure_push_constants_from_shaders();

    for (PushConstantRange& range : push_constants) {
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

void RenderComponent::write_transform_index_push_constant(uint32_t entity_index) {
    if (entity_index == InvalidUI32) {
        return;
    }
    set_push_constant_variable(
        hash64("transform_index"),
        reinterpret_cast<const std::byte*>(&entity_index),
        sizeof(entity_index));
}

void RenderComponent::initialize(
    Device* device,
    TransformComponent& transform,
    uint32_t entity_index) {
    (void)device;
    if (initialized) {
        return;
    }

    if (material == nullptr) {
        return;
    }

    if (mesh_id == InvalidUI32 && get_mesh() != nullptr) {
        mesh_id = get_mesh()->mesh_id();
    }

    ensure_push_constants_from_shaders();
    update_push_constants(transform, entity_index);
    initialized = true;
}

void RenderComponent::update_push_constants(
    TransformComponent& transform,
    uint32_t entity_index) {
    ensure_push_constants_from_shaders();
    write_transform_index_push_constant(entity_index);

    if (update_push_constant_function == nullptr) {
        return;
    }
    if (initialized && transform.transform_version() == last_push_constant_transform_version) {
        return;
    }

    update_push_constant_function(*this, transform);
    last_push_constant_transform_version = transform.transform_version();
}

void RenderComponent::update(
    Device* device,
    TransformComponent& transform,
    uint32_t entity_index) {
    initialize(device, transform, entity_index);
    update_push_constants(transform, entity_index);
}

}// namespace ocarina

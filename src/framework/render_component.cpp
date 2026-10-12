#include "render_component.h"
#include "material.h"
#include "mesh.h"
#include "resource_manager.h"
#include "rhi/shader_base.h"
#include "rhi/shader_program.h"
#include "core/hash.h"

#include <algorithm>
#include <cstring>

namespace ocarina {

void RenderComponent::set_mesh(Mesh* mesh) {
    mesh_id = mesh != nullptr ? mesh->mesh_id() : InvalidUI32;
    initialized = false;
}

void RenderComponent::set_material(Material* new_material) {
    if (material == new_material) {
        return;
    }
    material = new_material;
    initialized = false;
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
    if (data == nullptr || size == 0 || material == nullptr) {
        return;
    }

    ensure_push_constants_from_shaders();

    ShaderProgram* program = material->get_shader_program();
    if (program == nullptr) {
        return;
    }

    // Variable offsets come from shared shader reflection; only staging bytes live on the entity.
    for (const ShaderPushConstant& pc : program->push_constants()) {
        if (pc.size == 0) {
            continue;
        }

        const uint16_t range_offset = static_cast<uint16_t>(pc.offset);
        const uint16_t range_size = static_cast<uint16_t>(
            std::min<uint32_t>(pc.size, static_cast<uint32_t>(PushConstantRange::kMaxDataBytes)));

        for (const ShaderReflection::ShaderVariable& variable : pc.shader_variables) {
            if (hash64(variable.name) != name_id) {
                continue;
            }

            for (PushConstantRange& range : push_constants) {
                if (range.offset != range_offset || range.size != range_size) {
                    continue;
                }

                const size_t copy_size = std::min(size, static_cast<size_t>(variable.size));
                if (variable.offset + copy_size > range.size
                    || variable.offset + copy_size > PushConstantRange::kMaxDataBytes) {
                    return;
                }
                std::memcpy(range.data.data() + variable.offset, data, copy_size);
                return;
            }
        }
    }
}

void RenderComponent::set_push_constant_variable(
    const char* name,
    const std::byte* data,
    size_t size) {
    set_push_constant_variable(hash64(name), data, size);
}

void RenderComponent::initialize(Device* device) {
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
    initialized = true;
}

void RenderComponent::update_push_constants(uint32_t entity_index) {
    ensure_push_constants_from_shaders();
    if (update_push_constant_function != nullptr) {
        update_push_constant_function(*this, entity_index);
    }
}

void RenderComponent::update(Device* device, uint32_t entity_index) {
    if (!initialized) {
        initialize(device);
    }
    update_push_constants(entity_index);
}

}// namespace ocarina

#include "render_component.h"
#include "material.h"
#include "mesh.h"
#include "resource_manager.h"
#include "rhi/shader_program.h"
#include "core/hash.h"

#include <algorithm>
#include <cstring>

namespace ocarina {

void RenderComponent::set_mesh(Mesh* mesh) {
    mesh_id = mesh != nullptr ? mesh->mesh_id() : InvalidUI32;
}

void RenderComponent::set_material(Material* new_material) {
    if (material == new_material) {
        return;
    }
    material = new_material;
    push_constants_by_program.clear();
    active_push_constant_program_ = nullptr;
}

Mesh* RenderComponent::get_mesh() const noexcept {
    return ResourceManager::instance().get_mesh(mesh_id);
}

void RenderComponent::mark_push_constants_dirty() noexcept {
    for (auto& [program, block] : push_constants_by_program) {
        (void)program;
        block.dirty = true;
    }
}

RenderComponent::PushConstantBlock& RenderComponent::ensure_push_constants(ShaderProgram* program) {
    auto& block = push_constants_by_program[program];
    if (block.ranges.empty() && program != nullptr) {
        program->collect_push_constant_ranges(block.ranges);
        block.dirty = true;
    }
    return block;
}

void RenderComponent::update_push_constants_if_dirty(ShaderProgram* program, uint32_t entity_index) {
    if (program == nullptr) {
        return;
    }

    PushConstantBlock& block = ensure_push_constants(program);
    if (!block.dirty) {
        return;
    }

    active_push_constant_program_ = program;
    if (update_push_constant_function != nullptr) {
        update_push_constant_function(*this, entity_index);
    }
    active_push_constant_program_ = nullptr;
    block.dirty = false;
}

const ocarina_vector<PushConstantRange>* RenderComponent::push_constants_for(
    ShaderProgram* program) const noexcept {
    const auto it = push_constants_by_program.find(program);
    if (it == push_constants_by_program.end()) {
        return nullptr;
    }
    return &it->second.ranges;
}

void RenderComponent::set_push_constant_variable(
    uint64_t name_id,
    const std::byte* data,
    size_t size) {
    if (data == nullptr || size == 0 || active_push_constant_program_ == nullptr) {
        return;
    }

    ShaderProgram* program = active_push_constant_program_;
    PushConstantBlock& block = ensure_push_constants(program);

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

            for (PushConstantRange& range : block.ranges) {
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

}// namespace ocarina

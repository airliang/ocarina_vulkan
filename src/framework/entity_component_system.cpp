
#include "entity_component_system.h"
#include "math.h"

namespace ocarina {

EntityComponentSystem::EntityComponentSystem() {
    gpu_transforms_.resize(kDefaultGpuTransformCapacity);
    gpu_transform_versions_.assign(kDefaultGpuTransformCapacity, InvalidUI32);
}

EntityComponentSystem& EntityComponentSystem::instance() noexcept {
    static EntityComponentSystem ecs;
    return ecs;
}

void EntityComponentSystem::ensure_gpu_transform_capacity(size_t entity_count) {
    if (entity_count <= gpu_transforms_.size()) {
        return;
    }

    size_t new_capacity = gpu_transforms_.empty() ? kDefaultGpuTransformCapacity : gpu_transforms_.size();
    while (new_capacity < entity_count) {
        new_capacity *= 2;
    }

    gpu_transforms_.resize(new_capacity);
    gpu_transform_versions_.resize(new_capacity, InvalidUI32);
    mark_gpu_transforms_dirty();
}

void EntityComponentSystem::sync_gpu_transforms() {
    std::lock_guard<std::mutex> lock(components_mutex_);
    const size_t count = transform_components_.size();
    ensure_gpu_transform_capacity(count);

    for (size_t entity_index = 0; entity_index < count; ++entity_index) {
        const TransformComponent& transform = transform_components_[entity_index];
        const uint32_t version = transform.transform_version();
        if (version == gpu_transform_versions_[entity_index]) {
            continue;
        }

        const float4x4& world_matrix = transform.get_world_matrix();
        gpu_transforms_[entity_index].model_matrix = world_matrix;
        gpu_transforms_[entity_index].model_matrix_inverse = inverse(world_matrix);
        gpu_transform_versions_[entity_index] = version;
        gpu_transforms_dirty_ = true;
    }
}

}// namespace ocarina

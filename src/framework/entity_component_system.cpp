#include "entity_component_system.h"
#include "math.h"

namespace ocarina {

EntityComponentSystem::EntityComponentSystem()
    : render_components_(
          "ECS.RenderComponent.dense",
          "ECS.RenderComponent.dense_to_entity",
          "ECS.RenderComponent.sparse"),
      transform_components_(
          "ECS.TransformComponent.dense",
          "ECS.TransformComponent.dense_to_entity",
          "ECS.TransformComponent.sparse"),
      light_components_(
          "ECS.LightComponent.dense",
          "ECS.LightComponent.dense_to_entity",
          "ECS.LightComponent.sparse") {
    gpu_transforms_.resize(kDefaultGpuTransformCapacity);
    gpu_transform_versions_.assign(kDefaultGpuTransformCapacity, InvalidUI32);
}

EntityComponentSystem& EntityComponentSystem::instance() noexcept {
    static EntityComponentSystem ecs;
    return ecs;
}

void EntityComponentSystem::ensure_gpu_transform_capacity(size_t entity_index_exclusive) {
    if (entity_index_exclusive <= gpu_transforms_.size()) {
        return;
    }

    size_t new_capacity = gpu_transforms_.empty() ? kDefaultGpuTransformCapacity : gpu_transforms_.size();
    while (new_capacity < entity_index_exclusive) {
        new_capacity *= 2;
    }

    gpu_transforms_.resize(new_capacity);
    gpu_transform_versions_.resize(new_capacity, InvalidUI32);
    mark_gpu_transforms_dirty();
}

uint32_t EntityComponentSystem::create_entity() {
    std::lock_guard<std::recursive_mutex> lock(components_mutex_);

    uint32_t entity_index = InvalidUI32;
    if (!free_list_.empty()) {
        entity_index = free_list_.back();
        free_list_.pop_back();
        entities_[entity_index].alive = true;
    } else {
        entity_index = static_cast<uint32_t>(entities_.size());
        entities_.push_back(EntitySlot{0, true});
    }

    ++alive_count_;
    ensure_gpu_transform_capacity(static_cast<size_t>(entity_index) + 1);
    return entity_index;
}

void EntityComponentSystem::destroy_entity(uint32_t entity_index) {
    std::lock_guard<std::recursive_mutex> lock(components_mutex_);
    if (entity_index >= entities_.size() || !entities_[entity_index].alive) {
        return;
    }

    render_components_.remove(entity_index);
    transform_components_.remove(entity_index);
    light_components_.remove(entity_index);

    entities_[entity_index].alive = false;
    ++entities_[entity_index].generation;
    free_list_.push_back(entity_index);
    --alive_count_;
}

bool EntityComponentSystem::is_alive(uint32_t entity_index) const noexcept {
    return entity_index < entities_.size() && entities_[entity_index].alive;
}

void EntityComponentSystem::clear() {
    std::lock_guard<std::recursive_mutex> lock(components_mutex_);
    render_components_.clear();
    transform_components_.clear();
    light_components_.clear();
    entities_.clear();
    free_list_.clear();
    alive_count_ = 0;
    gpu_transforms_.assign(kDefaultGpuTransformCapacity, GPUTransform{});
    gpu_transform_versions_.assign(kDefaultGpuTransformCapacity, InvalidUI32);
    gpu_transforms_dirty_ = true;
}

RenderComponent& EntityComponentSystem::add_render_component(uint32_t entity_index) {
    std::lock_guard<std::recursive_mutex> lock(components_mutex_);
    OC_ASSERT(is_alive(entity_index));
    return render_components_.add(entity_index);
}

bool EntityComponentSystem::remove_render_component(uint32_t entity_index) {
    std::lock_guard<std::recursive_mutex> lock(components_mutex_);
    return render_components_.remove(entity_index);
}

bool EntityComponentSystem::has_render_component(uint32_t entity_index) const noexcept {
    return render_components_.has(entity_index);
}

RenderComponent* EntityComponentSystem::try_render_component(uint32_t entity_index) noexcept {
    return render_components_.try_get(entity_index);
}

const RenderComponent* EntityComponentSystem::try_render_component(uint32_t entity_index) const noexcept {
    return render_components_.try_get(entity_index);
}

RenderComponent& EntityComponentSystem::render_component(uint32_t entity_index) {
    return render_components_.get(entity_index);
}

const RenderComponent& EntityComponentSystem::render_component(uint32_t entity_index) const {
    return render_components_.get(entity_index);
}

TransformComponent& EntityComponentSystem::add_transform_component(uint32_t entity_index) {
    std::lock_guard<std::recursive_mutex> lock(components_mutex_);
    OC_ASSERT(is_alive(entity_index));
    TransformComponent& transform = transform_components_.add(entity_index);
    ensure_gpu_transform_capacity(static_cast<size_t>(entity_index) + 1);
    mark_gpu_transforms_dirty();
    return transform;
}

bool EntityComponentSystem::remove_transform_component(uint32_t entity_index) {
    std::lock_guard<std::recursive_mutex> lock(components_mutex_);
    const bool removed = transform_components_.remove(entity_index);
    if (removed) {
        mark_gpu_transforms_dirty();
    }
    return removed;
}

bool EntityComponentSystem::has_transform_component(uint32_t entity_index) const noexcept {
    return transform_components_.has(entity_index);
}

TransformComponent* EntityComponentSystem::try_transform_component(uint32_t entity_index) noexcept {
    return transform_components_.try_get(entity_index);
}

const TransformComponent* EntityComponentSystem::try_transform_component(uint32_t entity_index) const noexcept {
    return transform_components_.try_get(entity_index);
}

TransformComponent& EntityComponentSystem::transform_component(uint32_t entity_index) {
    return transform_components_.get(entity_index);
}

const TransformComponent& EntityComponentSystem::transform_component(uint32_t entity_index) const {
    return transform_components_.get(entity_index);
}

LightComponent& EntityComponentSystem::add_light_component(uint32_t entity_index) {
    std::lock_guard<std::recursive_mutex> lock(components_mutex_);
    OC_ASSERT(is_alive(entity_index));
    return light_components_.add(entity_index);
}

bool EntityComponentSystem::remove_light_component(uint32_t entity_index) {
    std::lock_guard<std::recursive_mutex> lock(components_mutex_);
    return light_components_.remove(entity_index);
}

bool EntityComponentSystem::has_light_component(uint32_t entity_index) const noexcept {
    return light_components_.has(entity_index);
}

LightComponent* EntityComponentSystem::try_light_component(uint32_t entity_index) noexcept {
    return light_components_.try_get(entity_index);
}

const LightComponent* EntityComponentSystem::try_light_component(uint32_t entity_index) const noexcept {
    return light_components_.try_get(entity_index);
}

LightComponent& EntityComponentSystem::light_component(uint32_t entity_index) {
    return light_components_.get(entity_index);
}

const LightComponent& EntityComponentSystem::light_component(uint32_t entity_index) const {
    return light_components_.get(entity_index);
}

void EntityComponentSystem::sync_gpu_transforms() {
    std::lock_guard<std::recursive_mutex> lock(components_mutex_);

    const size_t count = transform_components_.size();
    for (size_t dense_index = 0; dense_index < count; ++dense_index) {
        const uint32_t entity_index = transform_components_.entity_at_dense(
            static_cast<uint32_t>(dense_index));
        ensure_gpu_transform_capacity(static_cast<size_t>(entity_index) + 1);

        const TransformComponent& transform = transform_components_.dense()[dense_index];
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

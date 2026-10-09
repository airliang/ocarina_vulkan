#pragma once

#include "core/concepts.h"
#include "core/stl.h"
#include "math.h"
#include "math/basic_types.h"
#include "sparse_component_pool.h"
#include "render_component.h"
#include "transform_component.h"
#include "light_component.h"
#include <mutex>

namespace ocarina {

/// CPU mirror of `Transform` in `res/shaderlibrary/builtin/frame.hlsl`
/// (`StructuredBuffer<Transform> g_transforms` on FRAME_SET).
struct alignas(16) GPUTransform {
    float4x4 model_matrix{};
    float4x4 model_matrix_inverse{};
};

static_assert(sizeof(GPUTransform) == 128);

/// CPU mirror of `MaterialParams` in `res/shaderlibrary/builtin/material_params.hlsl`
/// (per-material `cbuffer material_ubo` on MATERIAL_SET).
struct alignas(16) MaterialParams {
    float4 baseColorFactor = make_float4(1.f, 1.f, 1.f, 1.f);
    float roughness = 1.f;
    float metallic = 0.f;
    float ao = 1.f;
    uint32_t albedoIndex = 0;
    uint32_t normalIndex = 0;
    uint32_t albedoSamplerIndex = 0;
    uint32_t normalSamplerIndex = 0;
    uint32_t metallicRoughnessIndex = 0xffffffffu;
    uint32_t metallicRoughnessSamplerIndex = 0;
    float padding[3] = {0.f, 0.f, 0.f};
};

static_assert(sizeof(MaterialParams) == 64);
static_assert(offsetof(MaterialParams, roughness) == 16);
static_assert(offsetof(MaterialParams, metallicRoughnessSamplerIndex) == 48);

/// Index-based ECS: entities are IDs; components live in per-type sparse pools.
/// GPU transforms stay dense, sized to max live entity index (holes allowed).
class EntityComponentSystem : public concepts::Noncopyable {
public:
    static constexpr size_t kDefaultGpuTransformCapacity = 1024;
    static constexpr const char* kTransformsBufferName = "g_transforms";

    static EntityComponentSystem& instance() noexcept;

    /// Allocate a new entity index (reuses free slots). Does not add components.
    [[nodiscard]] uint32_t create_entity();

    /// Destroy entity and remove all of its components.
    void destroy_entity(uint32_t entity_index);

    [[nodiscard]] bool is_alive(uint32_t entity_index) const noexcept;

    /// Number of currently alive entities.
    [[nodiscard]] uint32_t alive_entity_count() const noexcept { return alive_count_; }

    /// Highest entity index ever allocated + 1 (capacity of the densified GPU transform table).
    [[nodiscard]] uint32_t entity_index_capacity() const noexcept {
        return static_cast<uint32_t>(entities_.size());
    }

    // --- Render ---
    RenderComponent& add_render_component(uint32_t entity_index);
    bool remove_render_component(uint32_t entity_index);
    [[nodiscard]] bool has_render_component(uint32_t entity_index) const noexcept;
    [[nodiscard]] RenderComponent* try_render_component(uint32_t entity_index) noexcept;
    [[nodiscard]] const RenderComponent* try_render_component(uint32_t entity_index) const noexcept;
    [[nodiscard]] RenderComponent& render_component(uint32_t entity_index);
    [[nodiscard]] const RenderComponent& render_component(uint32_t entity_index) const;
    [[nodiscard]] SparseComponentPool<RenderComponent>& render_components() noexcept {
        return render_components_;
    }
    [[nodiscard]] const SparseComponentPool<RenderComponent>& render_components() const noexcept {
        return render_components_;
    }
    [[nodiscard]] size_t render_component_count() const noexcept { return render_components_.size(); }

    // --- Transform ---
    TransformComponent& add_transform_component(uint32_t entity_index);
    bool remove_transform_component(uint32_t entity_index);
    [[nodiscard]] bool has_transform_component(uint32_t entity_index) const noexcept;
    [[nodiscard]] TransformComponent* try_transform_component(uint32_t entity_index) noexcept;
    [[nodiscard]] const TransformComponent* try_transform_component(uint32_t entity_index) const noexcept;
    [[nodiscard]] TransformComponent& transform_component(uint32_t entity_index);
    [[nodiscard]] const TransformComponent& transform_component(uint32_t entity_index) const;
    [[nodiscard]] SparseComponentPool<TransformComponent>& transform_components() noexcept {
        return transform_components_;
    }
    [[nodiscard]] const SparseComponentPool<TransformComponent>& transform_components() const noexcept {
        return transform_components_;
    }
    [[nodiscard]] size_t transform_component_count() const noexcept {
        return transform_components_.size();
    }

    // --- Light (optional) ---
    LightComponent& add_light_component(uint32_t entity_index);
    bool remove_light_component(uint32_t entity_index);
    [[nodiscard]] bool has_light_component(uint32_t entity_index) const noexcept;
    [[nodiscard]] LightComponent* try_light_component(uint32_t entity_index) noexcept;
    [[nodiscard]] const LightComponent* try_light_component(uint32_t entity_index) const noexcept;
    [[nodiscard]] LightComponent& light_component(uint32_t entity_index);
    [[nodiscard]] const LightComponent& light_component(uint32_t entity_index) const;
    [[nodiscard]] SparseComponentPool<LightComponent>& light_components() noexcept {
        return light_components_;
    }
    [[nodiscard]] const SparseComponentPool<LightComponent>& light_components() const noexcept {
        return light_components_;
    }
    [[nodiscard]] size_t light_component_count() const noexcept { return light_components_.size(); }

    /// Dense CPU array for `StructuredBuffer<Transform> g_transforms` (index == entity index).
    [[nodiscard]] ocarina_vector<GPUTransform>& gpu_transforms() noexcept { return gpu_transforms_; }
    [[nodiscard]] const ocarina_vector<GPUTransform>& gpu_transforms() const noexcept {
        return gpu_transforms_;
    }

    [[nodiscard]] size_t gpu_transform_count() const noexcept { return gpu_transforms_.size(); }

    void mark_gpu_transforms_dirty() noexcept { gpu_transforms_dirty_ = true; }
    [[nodiscard]] bool gpu_transforms_dirty() const noexcept { return gpu_transforms_dirty_; }
    void clear_gpu_transforms_dirty() noexcept { gpu_transforms_dirty_ = false; }

    /// Refresh CPU GPUTransform slots from entities that have TransformComponent.
    void sync_gpu_transforms();

    void clear();

private:
    struct EntitySlot {
        uint32_t generation = 0;
        bool alive = false;
    };

    EntityComponentSystem();

    void ensure_gpu_transform_capacity(size_t entity_index_exclusive);

    ocarina_vector<EntitySlot> entities_{ocarina_pool_allocator<EntitySlot>("ECS.EntitySlot.entities")};
    ocarina_vector<uint32_t> free_list_{ocarina_pool_allocator<uint32_t>("ECS.EntitySlot.free_list")};
    uint32_t alive_count_ = 0;

    SparseComponentPool<RenderComponent> render_components_;
    SparseComponentPool<TransformComponent> transform_components_;
    SparseComponentPool<LightComponent> light_components_;

    ocarina_vector<GPUTransform> gpu_transforms_{
        ocarina_pool_allocator<GPUTransform>("ECS.GPUTransform.gpu_transforms")};
    ocarina_vector<uint32_t> gpu_transform_versions_{
        ocarina_pool_allocator<uint32_t>("ECS.GPUTransform.gpu_transform_versions")};
    bool gpu_transforms_dirty_ = true;
    mutable std::recursive_mutex components_mutex_;
};

}// namespace ocarina

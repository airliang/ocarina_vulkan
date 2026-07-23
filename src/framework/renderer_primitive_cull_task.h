#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "bounding_box.h"
#include "frustum.h"
#include "entity_component_system.h"
#include "scene.h"
#include "mesh.h"
#include "simd_frustum_cull.h"
#include "enki_task_debug.h"
#include "core/profiler.h"
#include "ext/enkiTS/src/TaskScheduler.h"
#include <atomic>

namespace ocarina {

class RendererPrimitiveCullTask : public enki::ITaskSet {
public:
    void prepare(uint32_t max_visible_count) {
        if (max_visible_count == 0) {
            return;
        }
        if (max_visible_count != cull_batch_results_.size()) {
            cull_batch_results_.resize(max_visible_count);
        }

        visible_counts_.store(0u, std::memory_order_relaxed);
    }

    void configure(
        const Scene* scene,
        const std::vector<uint32_t>* visible_cell_indices,
        uint32_t visible_cell_count,
        const Frustum* frustum,
        size_t max_cells_per_batch) {
        scene_ = scene;
        visible_cell_indices_ = visible_cell_indices;
        visible_cell_count_ = visible_cell_count;
        frustum_ = frustum;
        max_cells_per_batch_ = max_cells_per_batch;
        m_SetSize = visible_cell_count_;
        m_MinRange = static_cast<uint32_t>(max_cells_per_batch_);
    }

    void set_visible_entity_indices(const std::vector<uint32_t>& indices) {
        visible_entity_indices_ = indices;
    }

    void set_visible_entity_indices(const std::vector<uint32_t>& indices, uint32_t count) {
        visible_entity_indices_.assign(indices.begin(), indices.begin() + count);
    }

    void clear_visible_entity_indices() noexcept {
        visible_entity_indices_.clear();
    }

    void commit_visible_results() {
        const uint32_t count = visible_count();
        visible_entity_indices_.assign(cull_batch_results_.begin(), cull_batch_results_.begin() + count);
    }

    [[nodiscard]] const std::vector<uint32_t>& visible_entity_indices() const noexcept {
        return visible_entity_indices_;
    }

    [[nodiscard]] uint32_t visible_count() const noexcept {
        return visible_counts_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t execute_thread_id() const noexcept { return execute_thread_id_; }

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override {
        OC_PROFILE_FUNCTION;
        (void)threadnum;
        if (scene_ == nullptr || visible_cell_indices_ == nullptr || frustum_ == nullptr) {
            return;
        }

        EntityComponentSystem& ecs = EntityComponentSystem::instance();
        const uint32_t ecs_primitive_count = ecs.primitive_count();
        const std::vector<uint32_t>& scene_entities = scene_->entity_indices();
        const std::vector<SceneGridCell>& cells = scene_->grid_cells();

        const uint32_t end_cell = range.end;
        uint32_t cell_i = range.start;

        alignas(16) float min_x[4]{};
        alignas(16) float min_y[4]{};
        alignas(16) float min_z[4]{};
        alignas(16) float max_x[4]{};
        alignas(16) float max_y[4]{};
        alignas(16) float max_z[4]{};
        alignas(16) float valid_mask_f[4]{};

        for (; cell_i < end_cell; ++cell_i) {
            const uint32_t flat = (*visible_cell_indices_)[cell_i];
            if (flat >= cells.size()) {
                continue;
            }
            const SceneGridCell& cell = cells[flat];
            if (cell.entity_count == 0) {
                continue;
            }

            const uint32_t begin = cell.first_entity_index;
            const uint32_t end = begin + cell.entity_count;

            uint32_t i = begin;
            for (; i + 4 <= end; i += 4) {
                uint32_t entity_index[4] = {
                    scene_entities[i + 0],
                    scene_entities[i + 1],
                    scene_entities[i + 2],
                    scene_entities[i + 3],
                };

                for (int lane = 0; lane < 4; ++lane) {
                    valid_mask_f[lane] = 0.0f;
                    if (entity_index[lane] >= ecs_primitive_count) {
                        min_x[lane] = min_y[lane] = min_z[lane] = 0.0f;
                        max_x[lane] = max_y[lane] = max_z[lane] = 0.0f;
                        continue;
                    }

                    Primitive& primitive = ecs.primitive(entity_index[lane]);
                    TransformComponent& transform = ecs.transform_component(entity_index[lane]);
                    Mesh* mesh = primitive.get_mesh();
                    if (mesh == nullptr || !mesh->has_local_bounds()) {
                        valid_mask_f[lane] = -1.0f;
                        min_x[lane] = min_y[lane] = min_z[lane] = 0.0f;
                        max_x[lane] = max_y[lane] = max_z[lane] = 0.0f;
                        continue;
                    }

                    const BoundingBox world_bounds = mesh->get_local_bounds().transformed(transform.get_world_matrix());
                    if (!world_bounds.valid) {
                        valid_mask_f[lane] = -1.0f;
                        min_x[lane] = min_y[lane] = min_z[lane] = 0.0f;
                        max_x[lane] = max_y[lane] = max_z[lane] = 0.0f;
                        continue;
                    }

                    min_x[lane] = world_bounds.min.x;
                    min_y[lane] = world_bounds.min.y;
                    min_z[lane] = world_bounds.min.z;
                    max_x[lane] = world_bounds.max.x;
                    max_y[lane] = world_bounds.max.y;
                    max_z[lane] = world_bounds.max.z;
                }

                const uint32_t visible_mask = intersect_aabb4_soa(
                    *frustum_,
                    _mm_load_ps(min_x),
                    _mm_load_ps(min_y),
                    _mm_load_ps(min_z),
                    _mm_load_ps(max_x),
                    _mm_load_ps(max_y),
                    _mm_load_ps(max_z),
                    _mm_load_ps(valid_mask_f));

                for (int lane = 0; lane < 4; ++lane) {
                    if (entity_index[lane] >= ecs_primitive_count) {
                        continue;
                    }
                    if ((visible_mask & (1u << lane)) == 0u) {
                        continue;
                    }
                    const uint32_t write_slot = visible_counts_.fetch_add(1u, std::memory_order_relaxed);
                    cull_batch_results_[write_slot] = entity_index[lane];
                }
            }

            for (; i < end; ++i) {
                const uint32_t entity_index = scene_entities[i];
                if (entity_index >= ecs_primitive_count) {
                    continue;
                }

                Primitive& primitive = ecs.primitive(entity_index);
                TransformComponent& transform = ecs.transform_component(entity_index);
                Mesh* mesh = primitive.get_mesh();
                bool is_visible = true;
                if (mesh != nullptr && mesh->has_local_bounds()) {
                    const BoundingBox world_bounds = mesh->get_local_bounds().transformed(transform.get_world_matrix());
                    if (world_bounds.valid) {
                        is_visible = world_bounds.intersects(*frustum_);
                    }
                }

                if (!is_visible) {
                    continue;
                }

                const uint32_t write_slot = visible_counts_.fetch_add(1u, std::memory_order_relaxed);
                cull_batch_results_[write_slot] = entity_index;
            }
        }
    }

private:
    const Scene* scene_ = nullptr;
    const std::vector<uint32_t>* visible_cell_indices_ = nullptr;
    uint32_t visible_cell_count_ = 0;
    const Frustum* frustum_ = nullptr;
    size_t max_cells_per_batch_ = 1;

    std::vector<uint32_t> cull_batch_results_;
    std::vector<uint32_t> visible_entity_indices_;
    std::atomic<uint32_t> visible_counts_{0};
    uint64_t execute_thread_id_ = 0;
};

}// namespace ocarina

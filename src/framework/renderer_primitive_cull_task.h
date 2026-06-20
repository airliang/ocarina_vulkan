#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "bounding_box.h"
#include "frustum.h"
#include "primitive.h"
#include "transform_component.h"
#include "mesh.h"
#include "simd_frustum_cull.h"
#include "ext/enkiTS/src/TaskScheduler.h"
#include <atomic>

namespace ocarina {

class RendererPrimitiveCullTask : public enki::ITaskSet {
public:
    void prepare(uint32_t primitive_count) {
        if (primitive_count == 0) {
            return;
        }
        if (primitive_count != primitive_cull_batch_results_.size()) {
            primitive_cull_batch_results_.resize(primitive_count);
        }

        visible_counts_.store(0u, std::memory_order_relaxed);
    }

    void configure(
        std::vector<Primitive>* primitives,
        std::vector<TransformComponent>* transform_components,
        const std::vector<uint32_t>* primitive_cull_batch,
        const Frustum* frustum,
        size_t max_primitives_per_batch,
        size_t total_primitives_in_batch) {
        primitives_ = primitives;
        transform_components_ = transform_components;
        primitive_cull_batch_ = primitive_cull_batch;
        frustum_ = frustum;
        max_primitives_per_batch_ = max_primitives_per_batch;
        total_primitives_in_batch_ = total_primitives_in_batch;
        m_SetSize = static_cast<uint32_t>(total_primitives_in_batch_);
        m_MinRange = static_cast<uint32_t>(max_primitives_per_batch);
    }

    [[nodiscard]] const std::vector<uint32_t>& batch_results() const noexcept {
        return primitive_cull_batch_results_;
    }

    [[nodiscard]] uint32_t visible_count() const noexcept {
        return visible_counts_.load(std::memory_order_relaxed);
    }

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override {
        if (primitives_ == nullptr || transform_components_ == nullptr || primitive_cull_batch_ == nullptr || frustum_ == nullptr) {
            return;
        }

        const uint32_t end = range.end;
        uint32_t i = range.start;

        // Frostbite DOD: gather world bounds into SoA, then run 4-wide SIMD frustum tests.
        alignas(16) float min_x[4]{};
        alignas(16) float min_y[4]{};
        alignas(16) float min_z[4]{};
        alignas(16) float max_x[4]{};
        alignas(16) float max_y[4]{};
        alignas(16) float max_z[4]{};
        alignas(16) float valid_mask_f[4]{};

        for (; i + 4 <= end; i += 4) {
            uint32_t scene_index[4] = {
                (*primitive_cull_batch_)[i + 0],
                (*primitive_cull_batch_)[i + 1],
                (*primitive_cull_batch_)[i + 2],
                (*primitive_cull_batch_)[i + 3],
            };

            for (int lane = 0; lane < 4; ++lane) {
                valid_mask_f[lane] = 0.0f;
                if (scene_index[lane] >= primitives_->size() || scene_index[lane] >= transform_components_->size()) {
                    continue;
                }

                Primitive& primitive = (*primitives_)[scene_index[lane]];
                TransformComponent& transform = (*transform_components_)[scene_index[lane]];
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
                if (scene_index[lane] >= primitives_->size()) {
                    continue;
                }
                if ((visible_mask & (1u << lane)) == 0u) {
                    continue;
                }
                const uint32_t write_slot = visible_counts_.fetch_add(1u, std::memory_order_relaxed);
                primitive_cull_batch_results_[write_slot] = scene_index[lane];
            }
        }

        // Scalar tail.
        for (; i < end; ++i) {
            const uint32_t scene_index = (*primitive_cull_batch_)[i];
            if (scene_index >= primitives_->size() || scene_index >= transform_components_->size()) {
                continue;
            }

            Primitive& primitive = (*primitives_)[scene_index];
            TransformComponent& transform = (*transform_components_)[scene_index];
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
            primitive_cull_batch_results_[write_slot] = scene_index;
        }
    }

private:
    std::vector<Primitive>* primitives_ = nullptr;
    std::vector<TransformComponent>* transform_components_ = nullptr;
    const std::vector<uint32_t>* primitive_cull_batch_ = nullptr;
    const Frustum* frustum_ = nullptr;
    size_t max_primitives_per_batch_ = 1;
    uint32_t total_primitives_in_batch_ = 0;

    std::vector<uint32_t> primitive_cull_batch_results_;
    std::atomic<uint32_t> visible_counts_{0};
};

}// namespace ocarina

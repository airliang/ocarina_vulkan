#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "bounding_box.h"
#include "frustum.h"
#include "primitive.h"
#include "mesh.h"
#include "ext/enkiTS/src/TaskScheduler.h"
#include <atomic>
#include <immintrin.h>

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
        const std::vector<uint32_t>* primitive_cull_batch,
        const Frustum* frustum,
        size_t max_primitives_per_batch,
        size_t total_primitives_in_batch) {
        primitives_ = primitives;
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
        if (primitives_ == nullptr || primitive_cull_batch_ == nullptr || frustum_ == nullptr) {
            return;
        }

        const uint32_t end = range.end;
        uint32_t i = range.start;

        // 4-wide SIMD frustum test (plane vs AABB) for throughput.
        for (; i + 4 <= end; i += 4) {
            uint32_t scene_index[4] = {
                (*primitive_cull_batch_)[i + 0],
                (*primitive_cull_batch_)[i + 1],
                (*primitive_cull_batch_)[i + 2],
                (*primitive_cull_batch_)[i + 3],
            };

            BoundingBox bounds[4] = {};
            bool lane_valid[4] = {true, true, true, true};
            for (int lane = 0; lane < 4; ++lane) {
                if (scene_index[lane] >= primitives_->size()) {
                    lane_valid[lane] = false;
                    continue;
                }
                Primitive& primitive = (*primitives_)[scene_index[lane]];
                Mesh* mesh = primitive.get_mesh();
                if (mesh != nullptr && mesh->has_local_bounds()) {
                    bounds[lane] = mesh->get_local_bounds().transformed(primitive.get_world_matrix());
                } else {
                    bounds[lane] = {};
                }
            }

            // Start with all lanes visible, then eliminate by planes.
            __m128 visible_mask = _mm_castsi128_ps(_mm_set1_epi32(-1));
            for (int plane_index = 0; plane_index < Frustum::plane_count; ++plane_index) {
                const FrustumPlane& p = frustum_->plane(plane_index);

                // If a bounds is invalid => always visible for that lane.
                // We'll treat invalid bounds as passing the test by setting dot to +inf.
                const float nx = p.normal.x;
                const float ny = p.normal.y;
                const float nz = p.normal.z;
                const float d  = p.distance;

                float px_f[4], py_f[4], pz_f[4];
                for (int lane = 0; lane < 4; ++lane) {
                    if (!lane_valid[lane] || !bounds[lane].valid) {
                        px_f[lane] = 0.0f; py_f[lane] = 0.0f; pz_f[lane] = 0.0f;
                        continue;
                    }
                    px_f[lane] = (nx >= 0.0f) ? bounds[lane].max.x : bounds[lane].min.x;
                    py_f[lane] = (ny >= 0.0f) ? bounds[lane].max.y : bounds[lane].min.y;
                    pz_f[lane] = (nz >= 0.0f) ? bounds[lane].max.z : bounds[lane].min.z;
                }

                const __m128 px = _mm_set_ps(px_f[3], px_f[2], px_f[1], px_f[0]);
                const __m128 py = _mm_set_ps(py_f[3], py_f[2], py_f[1], py_f[0]);
                const __m128 pz = _mm_set_ps(pz_f[3], pz_f[2], pz_f[1], pz_f[0]);

                __m128 dist = _mm_add_ps(
                    _mm_add_ps(_mm_mul_ps(_mm_set1_ps(nx), px), _mm_mul_ps(_mm_set1_ps(ny), py)),
                    _mm_add_ps(_mm_mul_ps(_mm_set1_ps(nz), pz), _mm_set1_ps(d)));

                // For invalid bounds or invalid lane: force pass by setting dist to +inf.
                float inf_f[4] = {};
                for (int lane = 0; lane < 4; ++lane) {
                    inf_f[lane] = (!lane_valid[lane] || !bounds[lane].valid) ? 1e30f : 0.0f;
                }
                dist = _mm_add_ps(dist, _mm_set_ps(inf_f[3], inf_f[2], inf_f[1], inf_f[0]));

                const __m128 outside = _mm_cmplt_ps(dist, _mm_set1_ps(0.0f));
                // visible_mask &= ~outside
                visible_mask = _mm_andnot_ps(outside, visible_mask);
            }

            const int mask = _mm_movemask_ps(visible_mask);
            for (int lane = 0; lane < 4; ++lane) {
                if (!lane_valid[lane]) {
                    continue;
                }
                if ((mask & (1 << lane)) == 0) {
                    continue;
                }
                const uint32_t write_slot = visible_counts_.fetch_add(1u, std::memory_order_relaxed);
                primitive_cull_batch_results_[write_slot] = scene_index[lane];
            }
        }

        // Tail (scalar) using the same fast plane-vs-AABB test.
        for (; i < end; ++i) {
            const uint32_t scene_index = (*primitive_cull_batch_)[i];
            if (scene_index >= primitives_->size()) {
                continue;
            }
            Primitive& primitive = (*primitives_)[scene_index];
            Mesh* mesh = primitive.get_mesh();
            if (mesh != nullptr && mesh->has_local_bounds()) {
                const BoundingBox world_bounds = mesh->get_local_bounds().transformed(primitive.get_world_matrix());
                if (world_bounds.valid) {
                    bool visible = true;
                    for (int plane_index = 0; plane_index < Frustum::plane_count; ++plane_index) {
                        const FrustumPlane& p = frustum_->plane(plane_index);
                        const float3 pv = {
                            (p.normal.x >= 0.0f) ? world_bounds.max.x : world_bounds.min.x,
                            (p.normal.y >= 0.0f) ? world_bounds.max.y : world_bounds.min.y,
                            (p.normal.z >= 0.0f) ? world_bounds.max.z : world_bounds.min.z,
                        };
                        if (p.signed_distance(pv) < 0.0f) {
                            visible = false;
                            break;
                        }
                    }
                    if (!visible) {
                        continue;
                    }
                }
            }

            const uint32_t write_slot = visible_counts_.fetch_add(1u, std::memory_order_relaxed);
            primitive_cull_batch_results_[write_slot] = scene_index;
        }
    }

private:
    std::vector<Primitive>* primitives_ = nullptr;
    const std::vector<uint32_t>* primitive_cull_batch_ = nullptr;
    const Frustum* frustum_ = nullptr;
    size_t max_primitives_per_batch_ = 1;
    uint32_t total_primitives_in_batch_ = 0;

    std::vector<uint32_t> primitive_cull_batch_results_;
    std::atomic<uint32_t> visible_counts_{0};
};

}// namespace ocarina

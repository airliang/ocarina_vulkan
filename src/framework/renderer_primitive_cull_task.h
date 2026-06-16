#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "bounding_box.h"
#include "frustum.h"
#include "primitive.h"
#include "mesh.h"
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

        for (uint32_t batch_index = range.start; batch_index < range.end; ++batch_index) {
            const uint32_t scene_index = (*primitive_cull_batch_)[batch_index];
            if (scene_index >= primitives_->size()) {
                continue;
            }

            Primitive& primitive = (*primitives_)[scene_index];
            Mesh* mesh = primitive.get_mesh();
            bool is_visible = true;
            if (mesh != nullptr && mesh->has_local_bounds()) {
                const BoundingBox world_bounds = mesh->get_local_bounds().transformed(primitive.get_world_matrix());
                is_visible = !world_bounds.valid || world_bounds.intersects(*frustum_);
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
    const std::vector<uint32_t>* primitive_cull_batch_ = nullptr;
    const Frustum* frustum_ = nullptr;
    size_t max_primitives_per_batch_ = 1;
    uint32_t total_primitives_in_batch_ = 0;

    std::vector<uint32_t> primitive_cull_batch_results_;
    std::atomic<uint32_t> visible_counts_{0};
};

}// namespace ocarina

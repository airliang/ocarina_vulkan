#pragma once

#include "math/basic_types.h"
#include "transform.h"

namespace ocarina {

class TransformComponent {
public:
    void set_position(const float3& position) {
        position_ = position;
        transform_.set_position(position);
        transform_dirty_ = true;
    }

    void set_rotation(const quaternion& rotation) {
        rotation_ = rotation;
        transform_dirty_ = true;
    }

    void set_scale(const float3& scale) {
        scale_ = scale;
        transform_dirty_ = true;
    }

    [[nodiscard]] const float3& get_position() const noexcept { return position_; }
    [[nodiscard]] const quaternion& get_rotation() const noexcept { return rotation_; }
    [[nodiscard]] const float3& get_scale() const noexcept { return scale_; }

    [[nodiscard]] const float4x4& get_world_matrix() const {
        if (transform_dirty_) {
            transform_.set_TRS(position_, rotation_, scale_);
            transform_dirty_ = false;
            world_matrix_ = transform_.mat4x4();
        }
        return world_matrix_;
    }

    [[nodiscard]] const Transform<float4x4>& get_transform() const noexcept {
        return transform_;
    }

private:
    mutable float4x4 world_matrix_{};
    float3 position_{};
    quaternion rotation_{0.0f, 0.0f, 0.0f, 1.0f};
    float3 scale_{1.0f, 1.0f, 1.0f};
    mutable bool transform_dirty_ = true;
    mutable Transform<float4x4> transform_{};
};

}// namespace ocarina

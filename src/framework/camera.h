//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "sdl_event_listener.h"
#include "rhi/graphics_descriptions.h"
#include "math.h"
#include <SDL3/SDL.h>

namespace ocarina {

class OC_FRAMEWORK_API Camera : public SDLWindowEventListener {
public:
    Camera() = default;
    Camera(float fov, float aspect_ratio, float znear, float zfar);
    ~Camera() noexcept override = default;

    Camera(Camera&&) noexcept = delete;
    Camera& operator=(Camera&&) noexcept = delete;
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;

    float get_fov() const { return fov_; }
    void set_fov(float new_fov) {
        fov_ = new_fov;
        dirty_ = true;
    }
    float get_znear() const { return znear_; }
    void set_znear(float new_znear) {
        znear_ = new_znear;
        dirty_ = true;
    }
    float get_zfar() const { return zfar_; }
    void set_zfar(float new_zfar) {
        zfar_ = new_zfar;
        dirty_ = true;
    }

    float get_aspect_ratio() const { return aspect_ratio_; }
    void set_aspect_ratio(float new_aspect_ratio) {
        aspect_ratio_ = new_aspect_ratio;
        dirty_ = true;
    }

    void set_move_speed(float speed) noexcept { move_speed_ = speed; }
    [[nodiscard]] float get_move_speed() const noexcept { return move_speed_; }

    void set_look_sensitivity(float sensitivity) noexcept { look_sensitivity_ = sensitivity; }
    [[nodiscard]] float get_look_sensitivity() const noexcept { return look_sensitivity_; }

    void set_pan_sensitivity(float sensitivity) noexcept { pan_sensitivity_ = sensitivity; }
    [[nodiscard]] float get_pan_sensitivity() const noexcept { return pan_sensitivity_; }

    void set_position(const math3d::Vector3D& position);
    void set_target(const math3d::Vector3D& target);

    [[nodiscard]] const math3d::Vector3D& get_position() const noexcept { return position_; }
    [[nodiscard]] const math3d::Vector3D& get_target() const noexcept { return target_; }

    math3d::Matrix4 get_view_matrix();
    math3d::Matrix4 get_projection_matrix();

    void update(double dt) noexcept;

    void process_sdl_event(const SDL_Event& event) noexcept override;

private:
    void update_matrices();
    void calculate_view_matrix();
    void calculate_projection_matrix();
    void sync_angles_from_forward();
    void sync_target_from_angles();
    void clear_movement_keys() noexcept;
    [[nodiscard]] math3d::Vector3D get_forward() const noexcept;
    [[nodiscard]] math3d::Vector3D get_right() const noexcept;
    void apply_orbit_delta(float delta_yaw, float delta_pitch) noexcept;
    void apply_pan_delta(float delta_x, float delta_y) noexcept;
    void move_along_view(float distance) noexcept;

    float fov_ = 60.0f;
    float znear_ = 0.1f;
    float zfar_ = 100.0f;
    float aspect_ratio_ = 60.0f;

    float move_speed_ = 0.5f;
    float look_sensitivity_ = 0.003f;
    float pan_sensitivity_ = 0.005f;

    math3d::Vector3D position_{};
    math3d::Vector3D target_ = {0.0f, 0.0f, 1.0f};
    math3d::Vector3D up_ = {0.0f, 1.0f, 0.0f};

    float yaw_ = 0.0f;
    float pitch_ = 0.0f;

    math3d::Matrix4 view_matrix_;
    math3d::Matrix4 projection_matrix_;

    bool dirty_ = true;
    bool look_active_ = false;
    bool pan_active_ = false;
    bool move_forward_ = false;
    bool move_backward_ = false;
    bool move_left_ = false;
    bool move_right_ = false;
    bool move_up_ = false;
    bool move_down_ = false;
};

}// namespace ocarina

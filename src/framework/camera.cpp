//
// Created by Zero on 06/06/2022.
//

#include "camera.h"
#include "ext/imgui/imgui.h"
#include <algorithm>
#include <cmath>

namespace ocarina {

namespace {

constexpr float kPitchLimit = 89.0f * static_cast<float>(M_PI) / 180.0f;

math3d::Vector3D scale_vec(const math3d::Vector3D& v, float s) noexcept {
    return {v[0] * s, v[1] * s, v[2] * s};
}

float vec_length(const math3d::Vector3D& v) noexcept {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

bool imgui_blocks_mouse(bool look_active, bool pan_active) noexcept {
    if (look_active || pan_active) {
        return false;
    }
    if (ImGui::GetCurrentContext() == nullptr) {
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse && ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
}

bool imgui_wants_keyboard() noexcept {
    return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
}

}// namespace

Camera::Camera(float fov, float aspect_ratio, float znear, float zfar)
    : fov_(fov)
    , aspect_ratio_(aspect_ratio)
    , znear_(znear)
    , zfar_(zfar) {}

void Camera::set_position(const math3d::Vector3D& position) {
    position_ = position;
    sync_angles_from_forward();
    dirty_ = true;
}

void Camera::set_target(const math3d::Vector3D& target) {
    target_ = target;
    sync_angles_from_forward();
    dirty_ = true;
}

math3d::Vector3D Camera::get_forward() const noexcept {
    return {
        std::cos(pitch_) * std::sin(yaw_),
        std::sin(pitch_),
        std::cos(pitch_) * std::cos(yaw_),
    };
}

math3d::Vector3D Camera::get_right() const noexcept {
    return math3d::normalize(math3d::cross(up_, get_forward()));
}

void Camera::sync_angles_from_forward() {
    const math3d::Vector3D forward = math3d::normalize(math3d::operator-(target_, position_));
    pitch_ = std::asin(std::clamp(forward[1], -1.0f, 1.0f));
    yaw_ = std::atan2(forward[0], forward[2]);
}

void Camera::sync_target_from_angles() {
    target_ = math3d::operator+(position_, get_forward());
    dirty_ = true;
}

void Camera::clear_movement_keys() noexcept {
    move_forward_ = false;
    move_backward_ = false;
    move_left_ = false;
    move_right_ = false;
    move_up_ = false;
    move_down_ = false;
}

void Camera::apply_orbit_delta(float delta_yaw, float delta_pitch) noexcept {
    const math3d::Vector3D offset = math3d::operator-(position_, target_);
    const float distance = std::max(vec_length(offset), 1e-4f);

    yaw_ += delta_yaw;
    pitch_ = std::clamp(pitch_ + delta_pitch, -kPitchLimit, kPitchLimit);
    position_ = math3d::operator-(target_, scale_vec(get_forward(), distance));
    dirty_ = true;
}

void Camera::apply_pan_delta(float delta_x, float delta_y) noexcept {
    if (delta_x == 0.0f && delta_y == 0.0f) {
        return;
    }

    const math3d::Vector3D right = get_right();
    const math3d::Vector3D offset = math3d::operator+(
        scale_vec(right, -delta_x * pan_sensitivity_),
        scale_vec(up_, -delta_y * pan_sensitivity_));
    position_ = math3d::operator+(position_, offset);
    target_ = math3d::operator+(target_, offset);
    dirty_ = true;
}

void Camera::move_along_view(float distance) noexcept {
    if (distance == 0.0f) {
        return;
    }
    const math3d::Vector3D forward = get_forward();
    position_ = math3d::operator+(position_, scale_vec(forward, distance));
    sync_target_from_angles();
}

void Camera::update(double dt) noexcept {
    if (!look_active_ || dt <= 0.0) {
        return;
    }

    math3d::Vector3D move_delta = {0.0f, 0.0f, 0.0f};
    const math3d::Vector3D forward = get_forward();
    const math3d::Vector3D right = get_right();

    if (move_forward_) {
        move_delta = math3d::operator+(move_delta, forward);
    }
    if (move_backward_) {
        move_delta = math3d::operator-(move_delta, forward);
    }
    if (move_right_) {
        move_delta = math3d::operator+(move_delta, right);
    }
    if (move_left_) {
        move_delta = math3d::operator-(move_delta, right);
    }
    if (move_up_) {
        move_delta = math3d::operator+(move_delta, up_);
    }
    if (move_down_) {
        move_delta = math3d::operator-(move_delta, up_);
    }

    const float move_length = vec_length(move_delta);
    if (move_length > 0.0f) {
        const float distance = move_speed_ * static_cast<float>(dt);
        position_ = math3d::operator+(position_, scale_vec(move_delta, distance / move_length));
        sync_target_from_angles();
    }
}

void Camera::process_sdl_event(const SDL_Event& event) noexcept {
    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (imgui_blocks_mouse(look_active_, pan_active_)) {
            return;
        }
        if (event.button.button == SDL_BUTTON_RIGHT) {
            look_active_ = event.button.down;
            if (!look_active_) {
                clear_movement_keys();
            }
            return;
        }
        if (event.button.button == SDL_BUTTON_MIDDLE) {
            pan_active_ = event.button.down;
            return;
        }
        return;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        if (imgui_blocks_mouse(look_active_, pan_active_)) {
            return;
        }
        if (pan_active_) {
            apply_pan_delta(event.motion.xrel, event.motion.yrel);
            return;
        }
        if (look_active_) {
            apply_orbit_delta(event.motion.xrel * look_sensitivity_, -event.motion.yrel * look_sensitivity_);
        }
        return;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        if (imgui_blocks_mouse(look_active_, pan_active_)) {
            return;
        }
        move_along_view(move_speed_ * event.wheel.y);
        return;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        if (!look_active_ || imgui_wants_keyboard()) {
            return;
        }
        const bool pressed = event.key.down;
        switch (event.key.key) {
        case SDLK_W:
            move_forward_ = pressed;
            break;
        case SDLK_S:
            move_backward_ = pressed;
            break;
        case SDLK_A:
            move_left_ = pressed;
            break;
        case SDLK_D:
            move_right_ = pressed;
            break;
        case SDLK_E:
            move_up_ = pressed;
            break;
        case SDLK_Q:
            move_down_ = pressed;
            break;
        default:
            break;
        }
        return;
    }
    default:
        break;
    }
}

math3d::Matrix4 Camera::get_view_matrix() {
    update_matrices();
    return view_matrix_;
}

math3d::Matrix4 Camera::get_projection_matrix() {
    update_matrices();
    return projection_matrix_;
}

void Camera::update_matrices() {
    if (dirty_) {
        calculate_view_matrix();
        calculate_projection_matrix();
        dirty_ = false;
    }
}

void Camera::calculate_view_matrix() {
    view_matrix_ = math3d::Matrix4::look_at(position_, target_, up_);
}

void Camera::calculate_projection_matrix() {
    projection_matrix_ = math3d::Matrix4::perspective(math3d::deg_to_rad(fov_), aspect_ratio_, znear_, zfar_);
    // Vulkan NDC is +Y down; flip projection Y to match a +Y-up world.
    projection_matrix_(1, 1) = -projection_matrix_(1, 1);
}

}// namespace ocarina

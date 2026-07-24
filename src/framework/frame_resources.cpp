#include "frame_resources.h"
#include "bindless_texture_registry.h"
#include "camera.h"
#include "rhi/descriptor_set.h"
#include <cmath>

namespace ocarina {

namespace {

float3 normalize_or_default(const float3& v, const float3& fallback) noexcept {
    const float len_sq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len_sq < 1e-12f) {
        return fallback;
    }
    const float inv_len = 1.0f / std::sqrt(len_sq);
    return make_float3(v.x * inv_len, v.y * inv_len, v.z * inv_len);
}

}// namespace

FrameResources& FrameResources::instance() {
    static FrameResources s_instance;
    return s_instance;
}

DescriptorSet* FrameResources::get_global_descriptor_set(uint64_t name_id) const {
    std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);
    auto it = global_descriptor_sets_.find(name_id);
    return it == global_descriptor_sets_.end() ? nullptr : it->second;
}

DescriptorSet* FrameResources::get_global_descriptor_set(const std::string& name) const {
    return get_global_descriptor_set(hash64(name));
}

void FrameResources::add_global_descriptor_set(uint64_t name_id, DescriptorSet* descriptor_set) {
    std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);
    auto it = global_descriptor_sets_.find(name_id);
    if (it != global_descriptor_sets_.end()) {
        return;
    }
    global_descriptor_sets_.insert(std::make_pair(name_id, descriptor_set));
    rebuild_global_descriptor_sets_array_locked();
}

DescriptorSet* FrameResources::get_or_create_global_descriptor_set(
    uint32_t descriptor_set_index,
    const std::vector<uint64_t>& name_ids,
    ocarina::function<DescriptorSet* ()> create_descriptor_set) {
    std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);

    if (global_ubo_descriptor_set_ != nullptr) {
        return global_ubo_descriptor_set_;
    }

    global_ubo_descriptor_set_ = create_descriptor_set();
    global_ubo_descriptor_set_index_ = descriptor_set_index;

    for (uint64_t name_id : name_ids) {
        global_descriptor_sets_.insert(std::make_pair(name_id, global_ubo_descriptor_set_));
    }

    rebuild_global_descriptor_sets_array_locked();
    return global_ubo_descriptor_set_;
}

DescriptorSet* FrameResources::get_or_create_bindless_descriptor_set(
    uint32_t descriptor_set_index,
    const std::vector<uint64_t>& binding_name_ids,
    ocarina::function<DescriptorSet* ()> create_descriptor_set) {
    std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);

    if (bindless_descriptor_set_ != nullptr) {
        return bindless_descriptor_set_;
    }

    bindless_descriptor_set_ = create_descriptor_set();
    bindless_descriptor_set_index_ = descriptor_set_index;

    for (uint64_t name_id : binding_name_ids) {
        global_descriptor_sets_.insert(std::make_pair(name_id, bindless_descriptor_set_));
    }

    BindlessTextureRegistry::instance().for_each([this](uint32_t index, Texture* texture) {
        if (texture != nullptr) {
            bindless_descriptor_set_->update_bindless_texture_at_index(index, texture);
        }
    });

    rebuild_global_descriptor_sets_array_locked();
    return bindless_descriptor_set_;
}

DescriptorSet* FrameResources::get_global_ubo_descriptor_set() const {
    std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);
    return global_ubo_descriptor_set_;
}

DescriptorSet* FrameResources::get_bindless_descriptor_set() const {
    std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);
    return bindless_descriptor_set_;
}

void FrameResources::update_bindless_texture_at_index(uint32_t index, Texture* texture) {
    DescriptorSet* bindless_set = get_bindless_descriptor_set();
    if (bindless_set != nullptr && texture != nullptr && index != InvalidUI32) {
        bindless_set->update_bindless_texture_at_index(index, texture);
    }
}

bool FrameResources::is_global_descriptor_set_index(uint32_t set_index) const {
    std::lock_guard<std::mutex> lock(global_descriptor_sets_mutex_);
    if (global_ubo_descriptor_set_index_ != InvalidUI32 && set_index == global_ubo_descriptor_set_index_) {
        return true;
    }
    return bindless_descriptor_set_index_ != InvalidUI32 && set_index == bindless_descriptor_set_index_;
}

void FrameResources::set_sun_direction(const float3& direction) noexcept {
    const float3 n = normalize_or_default(direction, make_float3(-0.4f, -1.0f, -0.3f));
    global_ubo_.sun_direction = make_float4(n.x, n.y, n.z, 0.0f);
}

void FrameResources::set_sun_color(const float3& color) noexcept {
    global_ubo_.sun_color = make_float4(color.x, color.y, color.z, 1.0f);
}

void FrameResources::set_sun_intensity(float intensity) noexcept {
    global_ubo_.sun_intensity = intensity;
}

void FrameResources::set_light_position(const float3& position) noexcept {
    global_ubo_.light_pos = make_float4(position.x, position.y, position.z, 1.0f);
}

void FrameResources::upload_global_uniform_buffer(Camera* camera) {
    DescriptorSet* global_descriptor_set = get_global_descriptor_set("global_ubo");
    if (global_descriptor_set == nullptr) {
        return;
    }

    if (camera != nullptr) {
        global_ubo_.projection_matrix = camera->get_projection_matrix().transpose();
        global_ubo_.view_matrix = camera->get_view_matrix().transpose();
        const math3d::Vector3D& cam_position = camera->get_position();
        global_ubo_.camera_pos = make_float4(cam_position[0], cam_position[1], cam_position[2], 1.0f);
    }

    // Keep sun direction normalized for the shader.
    set_sun_direction(make_float3(
        global_ubo_.sun_direction.x,
        global_ubo_.sun_direction.y,
        global_ubo_.sun_direction.z));

    global_descriptor_set->update_buffer(
        hash64("global_ubo"),
        &global_ubo_,
        sizeof(GlobalUniformBuffer));
}

void FrameResources::update_per_frame(double dt, Camera* camera) {
    upload_global_uniform_buffer(camera);
    if (update_) {
        update_(*this, dt);
    }
}

void FrameResources::rebuild_global_descriptor_sets_array_locked() {
    global_descriptor_sets_array_.clear();
    for (const auto& pair : global_descriptor_sets_) {
        if (pair.second == bindless_descriptor_set_ || pair.second == global_ubo_descriptor_set_) {
            continue;
        }
        if (std::find(global_descriptor_sets_array_.begin(), global_descriptor_sets_array_.end(), pair.second) ==
            global_descriptor_sets_array_.end()) {
            global_descriptor_sets_array_.push_back(pair.second);
        }
    }
}

}// namespace ocarina

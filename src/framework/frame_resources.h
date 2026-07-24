#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/concepts.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/pipeline_state.h"
#include "rhi/resources/texture.h"
#include "global_uniform_buffer.h"
#include <algorithm>
#include <mutex>

namespace ocarina {
class DescriptorSetLayout;
class DescriptorSet;
class Camera;

class OC_FRAMEWORK_API FrameResources : public concepts::Noncopyable {
public:
    using UpdateCallback = ocarina::function<void(FrameResources&, double)>;

    static FrameResources& instance();

    void add_global_descriptor_set(uint64_t name_id, DescriptorSet* descriptor_set);

    void add_global_descriptor_set(const std::string& name, DescriptorSet* descriptor_set) {
        add_global_descriptor_set(hash64(name), descriptor_set);
    }

    DescriptorSet* get_global_descriptor_set(uint64_t name_id) const;
    DescriptorSet* get_global_descriptor_set(const std::string& name) const;

    DescriptorSet* get_or_create_global_descriptor_set(
        uint32_t descriptor_set_index,
        const std::vector<uint64_t>& binding_name_ids,
        ocarina::function<DescriptorSet* ()> create_descriptor_set);

    DescriptorSet* get_global_ubo_descriptor_set() const;
    uint32_t global_ubo_descriptor_set_index() const { return global_ubo_descriptor_set_index_; }
    bool has_global_ubo_descriptor_set() const { return global_ubo_descriptor_set_ != nullptr; }

    const std::vector<DescriptorSet*>& global_descriptor_sets_array() const {
        return global_descriptor_sets_array_;
    }

    /// Shared bindless texture array descriptor set (one per process, bound at bindless_descriptor_set_index()).
    DescriptorSet* get_or_create_bindless_descriptor_set(
        uint32_t descriptor_set_index,
        const std::vector<uint64_t>& binding_name_ids,
        ocarina::function<DescriptorSet* ()> create_descriptor_set);

    DescriptorSet* get_bindless_descriptor_set() const;
    uint32_t bindless_descriptor_set_index() const { return bindless_descriptor_set_index_; }
    bool has_bindless_descriptor_set() const { return bindless_descriptor_set_ != nullptr; }

    /// Writes into the global bindless set using the process-wide BindlessTextureRegistry index.
    void update_bindless_texture_at_index(uint32_t index, Texture* texture);

    bool is_global_descriptor_set_index(uint32_t set_index) const;

    [[nodiscard]] GlobalUniformBuffer& global_uniform_buffer() noexcept { return global_ubo_; }
    [[nodiscard]] const GlobalUniformBuffer& global_uniform_buffer() const noexcept { return global_ubo_; }

    void set_sun_direction(const float3& direction) noexcept;
    void set_sun_color(const float3& color) noexcept;
    void set_sun_intensity(float intensity) noexcept;
    void set_light_position(const float3& position) noexcept;

    /// Optional extension hook (global UBO is always updated by the framework first).
    void set_update_callback(UpdateCallback cb) {
        update_ = std::move(cb);
    }

    /// Called on the render thread each frame. Uploads `global_ubo` from @p camera (if set).
    void update_per_frame(double dt, Camera* camera = nullptr);

private:
    FrameResources() = default;

    void rebuild_global_descriptor_sets_array_locked();
    void upload_global_uniform_buffer(Camera* camera);

    mutable std::mutex global_descriptor_sets_mutex_;
    std::unordered_map<uint64_t, DescriptorSet*> global_descriptor_sets_;
    std::vector<DescriptorSet*> global_descriptor_sets_array_;

    DescriptorSet* global_ubo_descriptor_set_ = nullptr;
    uint32_t global_ubo_descriptor_set_index_ = InvalidUI32;

    DescriptorSet* bindless_descriptor_set_ = nullptr;
    uint32_t bindless_descriptor_set_index_ = InvalidUI32;

    GlobalUniformBuffer global_ubo_{};
    UpdateCallback update_ = nullptr;
};

}// namespace ocarina

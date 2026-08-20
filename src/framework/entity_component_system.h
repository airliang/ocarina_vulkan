
#pragma once
#include "core/concepts.h"
#include "core/stl.h"
#include "math.h"
#include "math/basic_types.h"
#include "primitive.h"
#include "render_component.h"
#include "transform_component.h"
#include "light_component.h"
#include <mutex>

namespace ocarina {

/// CPU mirror of `Transform` in `res/shaderlibrary/builtin/transform.hlsl`
/// (`StructuredBuffer<Transform> transforms` on SCENE_SET).
struct alignas(16) GPUTransform {
    float4x4 model_matrix{};
    float4x4 model_matrix_inverse{};
};

static_assert(sizeof(GPUTransform) == 128);

/// CPU mirror of `MaterialParams` in `res/shaderlibrary/builtin/material_params.hlsl`
/// (`StructuredBuffer<MaterialParams> g_materials` on MATERIAL_SET).
/// HLSL structured-buffer stride is 16-byte aligned, so this is 64 bytes (12 bytes pad).
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
    float padding[3] = { 0.f, 0.f, 0.f };
};

static_assert(sizeof(MaterialParams) == 64);
static_assert(offsetof(MaterialParams, roughness) == 16);
static_assert(offsetof(MaterialParams, metallicRoughnessSamplerIndex) == 48);

class EntityComponentSystem : public concepts::Noncopyable {
public:
    static constexpr size_t kDefaultGpuTransformCapacity = 1024;
    static constexpr const char* kTransformsBufferName = "transforms";
    static constexpr size_t kDefaultMaterialParamsCapacity = 256;
    static constexpr const char* kMaterialsBufferName = "g_materials";

    static EntityComponentSystem& instance() noexcept;

    template<typename... Args>
    uint32_t emplace_primitive(Args&&... args) {
        std::lock_guard<std::mutex> lock(components_mutex_);
        const uint32_t entity_index = static_cast<uint32_t>(primitives_.size());
        render_components_.emplace_back();
        transform_components_.emplace_back();
        primitives_.emplace_back(OC_FORWARD(args)...);
        primitives_.back().set_entity_index(entity_index);
        ensure_gpu_transform_capacity(primitives_.size());
        mark_gpu_transforms_dirty();
        return entity_index;
    }

    uint32_t emplace_primitive(Primitive&& primitive) {
        std::lock_guard<std::mutex> lock(components_mutex_);
        const uint32_t entity_index = static_cast<uint32_t>(primitives_.size());
        render_components_.emplace_back();
        transform_components_.emplace_back();
        primitives_.push_back(std::move(primitive));
        primitives_.back().set_entity_index(entity_index);
        ensure_gpu_transform_capacity(primitives_.size());
        mark_gpu_transforms_dirty();
        return entity_index;
    }

    [[nodiscard]] uint32_t allocate_material_buffer_region(uint32_t size) {
        const uint32_t offset = static_cast<uint32_t>(material_parameters_buffer_.size());
        material_parameters_buffer_.resize(offset + size);
        return offset;
    }

    [[nodiscard]] std::vector<uint8_t>& material_parameters_buffer() noexcept {
        return material_parameters_buffer_;
    }

    [[nodiscard]] const std::vector<uint8_t>& material_parameters_buffer() const noexcept {
        return material_parameters_buffer_;
    }

    void resize_render_components(size_t count) {
        std::lock_guard<std::mutex> lock(components_mutex_);
        render_components_.resize(count);
    }

    void resize_transform_components(size_t count) {
        std::lock_guard<std::mutex> lock(components_mutex_);
        transform_components_.resize(count);
        ensure_gpu_transform_capacity(count);
        mark_gpu_transforms_dirty();
    }

    void resize_light_components(size_t count) {
        std::lock_guard<std::mutex> lock(components_mutex_);
        light_components_.resize(count);
    }

    void resize(size_t count) {
        resize_render_components(count);
        resize_transform_components(count);
        resize_light_components(count);
    }

    [[nodiscard]] size_t render_component_count() const noexcept {
        return render_components_.size();
    }

    [[nodiscard]] size_t transform_component_count() const noexcept {
        return transform_components_.size();
    }

    [[nodiscard]] size_t light_component_count() const noexcept {
        return light_components_.size();
    }

    [[nodiscard]] uint32_t primitive_count() const noexcept {
        return static_cast<uint32_t>(primitives_.size());
    }

    [[nodiscard]] RenderComponent& render_component(uint32_t entity_index) {
        return render_components_[entity_index];
    }

    [[nodiscard]] const RenderComponent& render_component(uint32_t entity_index) const {
        return render_components_[entity_index];
    }

    [[nodiscard]] TransformComponent& transform_component(uint32_t entity_index) {
        return transform_components_[entity_index];
    }

    [[nodiscard]] const TransformComponent& transform_component(uint32_t entity_index) const {
        return transform_components_[entity_index];
    }

    [[nodiscard]] LightComponent& light_component(uint32_t entity_index) {
        return light_components_[entity_index];
    }

    [[nodiscard]] const LightComponent& light_component(uint32_t entity_index) const {
        return light_components_[entity_index];
    }

    [[nodiscard]] Primitive& primitive(uint32_t entity_index) {
        return primitives_[entity_index];
    }

    [[nodiscard]] const Primitive& primitive(uint32_t entity_index) const {
        return primitives_[entity_index];
    }

    [[nodiscard]] std::vector<RenderComponent>& render_components() noexcept {
        return render_components_;
    }

    [[nodiscard]] const std::vector<RenderComponent>& render_components() const noexcept {
        return render_components_;
    }

    [[nodiscard]] std::vector<TransformComponent>& transform_components() noexcept {
        return transform_components_;
    }

    [[nodiscard]] const std::vector<TransformComponent>& transform_components() const noexcept {
        return transform_components_;
    }

    [[nodiscard]] std::vector<LightComponent>& light_components() noexcept {
        return light_components_;
    }

    [[nodiscard]] const std::vector<LightComponent>& light_components() const noexcept {
        return light_components_;
    }

    [[nodiscard]] std::vector<Primitive>& primitives() noexcept {
        return primitives_;
    }

    [[nodiscard]] const std::vector<Primitive>& primitives() const noexcept {
        return primitives_;
    }

    /// Dense CPU array for `StructuredBuffer<Transform> transforms` (index == entity index).
    [[nodiscard]] std::vector<GPUTransform>& gpu_transforms() noexcept { return gpu_transforms_; }
    [[nodiscard]] const std::vector<GPUTransform>& gpu_transforms() const noexcept {
        return gpu_transforms_;
    }

    [[nodiscard]] size_t gpu_transform_count() const noexcept {
        return transform_components_.size();
    }

    void mark_gpu_transforms_dirty() noexcept { gpu_transforms_dirty_ = true; }
    [[nodiscard]] bool gpu_transforms_dirty() const noexcept { return gpu_transforms_dirty_; }
    void clear_gpu_transforms_dirty() noexcept { gpu_transforms_dirty_ = false; }

    /// Refresh CPU GPUTransform slots from TransformComponents; sets dirty if any slot changed.
    void sync_gpu_transforms();

private:
    EntityComponentSystem();

    void ensure_gpu_transform_capacity(size_t entity_count);

    std::vector<Primitive> primitives_;
    std::vector<RenderComponent> render_components_;
    std::vector<TransformComponent> transform_components_;
    std::vector<LightComponent> light_components_;
    std::vector<uint8_t> material_parameters_buffer_;

    std::vector<GPUTransform> gpu_transforms_;
    std::vector<uint32_t> gpu_transform_versions_;
    bool gpu_transforms_dirty_ = true;
    mutable std::mutex components_mutex_;
};

}// namespace ocarina

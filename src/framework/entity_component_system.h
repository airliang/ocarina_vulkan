
#pragma once
#include "core/concepts.h"
#include "core/stl.h"
#include "primitive.h"
#include "render_component.h"
#include "transform_component.h"
#include "light_component.h"

namespace ocarina {

class EntityComponentSystem : public concepts::Noncopyable {
public:
    static EntityComponentSystem& instance() noexcept;

    template<typename... Args>
    uint32_t emplace_primitive(Args&&... args) {
        const uint32_t entity_index = static_cast<uint32_t>(primitives_.size());
        render_components_.emplace_back();
        transform_components_.emplace_back();
        light_components_.emplace_back();
        primitives_.emplace_back(OC_FORWARD(args)...);
        primitives_.back().set_entity_index(entity_index);
        return entity_index;
    }

    uint32_t emplace_primitive(Primitive&& primitive) {
        const uint32_t entity_index = static_cast<uint32_t>(primitives_.size());
        render_components_.emplace_back();
        transform_components_.emplace_back();
        light_components_.emplace_back();
        primitives_.push_back(std::move(primitive));
        primitives_.back().set_entity_index(entity_index);
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
        render_components_.resize(count);
    }

    void resize_transform_components(size_t count) {
        transform_components_.resize(count);
    }

    void resize_light_components(size_t count) {
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

private:
    EntityComponentSystem() = default;

    std::vector<Primitive> primitives_;
    std::vector<RenderComponent> render_components_;
    std::vector<TransformComponent> transform_components_;
    std::vector<LightComponent> light_components_;
    std::vector<uint8_t> material_parameters_buffer_;
};

}// namespace ocarina

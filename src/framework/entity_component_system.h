
#pragma once
#include "core/concepts.h"
#include "core/stl.h"
#include "render_component.h"
#include "transform_component.h"

namespace ocarina {

class EntityComponentSystem : public concepts::Noncopyable {
public:
    void resize_render_components(size_t count) {
        render_components_.resize(count);
    }

    void resize_transform_components(size_t count) {
        transform_components_.resize(count);
    }

    void resize(size_t count) {
        resize_render_components(count);
        resize_transform_components(count);
    }

    [[nodiscard]] size_t render_component_count() const noexcept {
        return render_components_.size();
    }

    [[nodiscard]] size_t transform_component_count() const noexcept {
        return transform_components_.size();
    }

    [[nodiscard]] RenderComponent& render_component(uint32_t index) {
        return render_components_[index];
    }

    [[nodiscard]] const RenderComponent& render_component(uint32_t index) const {
        return render_components_[index];
    }

    [[nodiscard]] TransformComponent& transform_component(uint32_t index) {
        return transform_components_[index];
    }

    [[nodiscard]] const TransformComponent& transform_component(uint32_t index) const {
        return transform_components_[index];
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

private:
    std::vector<RenderComponent> render_components_;
    std::vector<TransformComponent> transform_components_;
};

}// namespace ocarina

//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/stl.h"
#include "graphics_descriptions.h"
#include "rendertarget.h"
#include "pipeline_state.h"

namespace ocarina {
class Material;
class VertexBuffer;
class IndexBuffer;
class DescriptorSetWriter;
class DescriptorSet;
struct RHIPipeline;
class CommandBuffer;
class Texture;

struct DescriptorSetsBinding
{
    std::vector<DescriptorSet*> descriptor_sets;
    uint32_t first_set = 0;
    uint32_t descriptor_set_count = 0;
};

struct PipelineRenderQueue
{
    std::list<uint32_t> draw_call_items;

    void clear()
    {
        draw_call_items.clear();
    }
};

struct GlobalUBO
{
    float4x4 view_matrix = {1.0f};
    float4x4 projection_matrix = {1.0f};
};

class OC_RHI_API RHIRenderPass {
public:
    RHIRenderPass(const RenderPassCreation &render_pass_creation);
    virtual ~RHIRenderPass();

    void clear_draw_call_items();

    void add_draw_call(uint32_t render_component_index, const PipelineState& pipeline_state);

    [[nodiscard]] RenderTarget* render_target() noexcept { return render_target_; }
    [[nodiscard]] const RenderTarget* render_target() const noexcept { return render_target_; }

    /// Swapchain path: target is the swapchain backbuffer.
    [[nodiscard]] bool is_swapchain_renderpass() const {
        return render_target_ != nullptr && render_target_->is_swapchain();
    }

    /// Offscreen path: target is one or more textures.
    [[nodiscard]] bool is_offscreen_renderpass() const {
        return render_target_ != nullptr && render_target_->is_texture();
    }

    void set_viewport(const float4& viewport) noexcept {
        viewport_ = viewport;
    }

    void set_scissor(const int4& scissor) noexcept {
        scissor_ = scissor;
    }

    handle_ty get_command_buffer() const
    {
        return command_buffer_;
    }

    OC_MAKE_MEMBER_GETTER(size, )
    OC_MAKE_MEMBER_GETTER(scissor, )
    OC_MAKE_MEMBER_GETTER(viewport, )
    OC_MAKE_MEMBER_GETTER(clear_color, )
    OC_MAKE_MEMBER_GETTER(clear_depth, )
    OC_MAKE_MEMBER_GETTER(clear_stencil, )
    OC_MAKE_MEMBER_GETTER(clear_color_attachment, )
    OC_MAKE_MEMBER_GETTER(clear_depth_attachment, )
    OC_MAKE_MEMBER_GETTER(present_swapchain, )

    [[nodiscard]] uint32_t color_attachment_count() const noexcept {
        return render_target_ != nullptr ? render_target_->color_attachment_count() : 0;
    }

    [[nodiscard]] Texture* color_attachment(uint32_t index) const {
        return render_target_ != nullptr ? render_target_->color_attachment(index) : nullptr;
    }

    [[nodiscard]] Texture* depth_attachment() const {
        return render_target_ != nullptr ? render_target_->depth_attachment() : nullptr;
    }

    [[nodiscard]] bool is_use_swapchain_framebuffer() const {
        return is_swapchain_renderpass();
    }

    void update_swapchain_extent(uint2 extent) {
        if (!is_swapchain_renderpass()) {
            return;
        }
        size_ = extent;
        scissor_ = {0, 0, static_cast<int>(extent.x), static_cast<int>(extent.y)};
        viewport_ = {0, 0, static_cast<float>(extent.x), static_cast<float>(extent.y)};
    }

    const std::unordered_map<PipelineState, PipelineRenderQueue*, PipelineStateHash>& pipeline_render_queues() const {
        return pipeline_render_queues_;
    }

protected:
    RenderTarget* render_target_ = nullptr;

    float4 viewport_ = {0, 0, 0, 0};
    int4 scissor_ = {0, 0, 0, 0};
    uint2 size_ = {0, 0};

    std::string name_ = "RHIRenderPass";

    float4 clear_color_ = {0.025f, 0.025f, 0.025f, 1.0f};
    float clear_depth_ = 1.0f;
    uint32_t clear_stencil_ = 0;
    bool clear_color_attachment_ = true;
    bool clear_depth_attachment_ = true;
    bool present_swapchain_ = true;

    std::unordered_map<PipelineState, PipelineRenderQueue*, PipelineStateHash> pipeline_render_queues_;
    GlobalUBO global_ubo_data_ = {};
    handle_ty command_buffer_ = 0;
};

}// namespace ocarina

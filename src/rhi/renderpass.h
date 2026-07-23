//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/stl.h"
#include "graphics_descriptions.h"
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
    RHIRenderPass(const RenderPassCreation &render_pass_creation) {}
    virtual ~RHIRenderPass();

    void clear_draw_call_items();

    void add_draw_call(uint32_t render_component_index, const PipelineState& pipeline_state);

    void add_color_attachment(Texture* texture) {
        OC_ASSERT(color_attachment_count_ < kMaxColorAttachments);
        color_attachments_[color_attachment_count_++] = texture;
    }

    /// Swapchain path: no explicit color/depth attachments (backbuffer + swapchain depth).
    bool is_swapchain_renderpass() const
    {
        return color_attachment_count_ == 0 && depth_attachment_ == nullptr;
    }

    /// Offscreen path: explicit color and/or depth attachments.
    bool is_offscreen_renderpass() const
    {
        return color_attachment_count_ > 0 || depth_attachment_ != nullptr;
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
    OC_MAKE_MEMBER_GETTER(color_attachment_count, )
    OC_MAKE_MEMBER_GETTER(clear_color, )
    OC_MAKE_MEMBER_GETTER(clear_depth, )
    OC_MAKE_MEMBER_GETTER(clear_stencil, )
    OC_MAKE_MEMBER_GETTER(swapchain_clear_color, )
    OC_MAKE_MEMBER_GETTER(swapchain_clear_depth, )
    OC_MAKE_MEMBER_GETTER(swapchain_clear_stencil, )

    Texture* color_attachment(uint32_t index) const {
        return index < color_attachment_count_ ? color_attachments_[index] : nullptr;
    }

    Texture* depth_attachment() const {
        return depth_attachment_;
    }

    bool is_use_swapchain_framebuffer() const {
        return color_attachment_count_ == 0;
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

    float4 viewport_ = {0, 0, 0, 0};
    int4 scissor_ = {0, 0, 0, 0};
    uint2 size_ = {0, 0};

    std::string name_ = "RHIRenderPass";

    uint32_t color_attachment_count_ = 0;
    constexpr static const int kMaxColorAttachments = RenderPassCreation::MAX_COLOR_ATTACHMENTS;
    Texture* color_attachments_[kMaxColorAttachments] = {};
    Texture* depth_attachment_ = nullptr;

    float4 clear_color_ = {0.025f, 0.025f, 0.025f, 1.0f};
    float clear_depth_ = 1.0f;
    uint32_t clear_stencil_ = 0;
    float4 swapchain_clear_color_ = {0.025f, 0.025f, 0.025f, 1.0f};
    float swapchain_clear_depth_ = 1.0f;
    uint32_t swapchain_clear_stencil_ = 0;

    std::unordered_map<PipelineState, PipelineRenderQueue*, PipelineStateHash> pipeline_render_queues_;
    GlobalUBO global_ubo_data_ = {};
    handle_ty command_buffer_ = 0;
};

}// namespace ocarina

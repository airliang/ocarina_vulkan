//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/stl.h"
#include "graphics_descriptions.h"

namespace ocarina {
struct PipelineState;
class Material;
class VertexBuffer;
class IndexBuffer;
class RenderTarget;
class DescriptorSetWriter;
class DescriptorSet;
struct RHIPipeline;
class CommandBuffer;

struct DescriptorSetsBinding
{
    std::vector<DescriptorSet*> descriptor_sets;
    uint32_t first_set = 0;
    uint32_t descriptor_set_count = 0;
};

struct PipelineRenderQueue
{
    std::list<uint32_t> draw_call_items;
    RHIPipeline *pipeline_line = nullptr;

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

    void add_draw_call(uint32_t render_component_index, RHIPipeline* pipeline);

    void add_render_target(RenderTarget* render_target) {
        OC_ASSERT(render_target_count_ < kMaxRenderTargets);
        render_target_[render_target_count_++] = render_target;
    }

    bool is_swapchain_renderpass() const
    {
        return render_target_count_ == 0;
    }

    handle_ty get_command_buffer() const
    {
        return command_buffer_;
    }

    OC_MAKE_MEMBER_GETTER(size, )
    OC_MAKE_MEMBER_GETTER(scissor, )
    OC_MAKE_MEMBER_GETTER(viewport, )
    OC_MAKE_MEMBER_GETTER(render_target_count, )

    bool is_use_swapchain_framebuffer() const {
        return render_target_count_ == 0;
    }

    const std::unordered_map<RHIPipeline*, PipelineRenderQueue*>& pipeline_render_queues() const {
        return pipeline_render_queues_;
    }
protected:
    

    float4 viewport_ = {0, 0, 0, 0};
    int4 scissor_ = {0, 0, 0, 0};
    uint2 size_ = {0, 0};

    std::string name_ = "RHIRenderPass";

    //if render_target_count == 0, use the swapchain backbuffer as render target
    uint32_t render_target_count_ = 0;
    constexpr static const int kMaxRenderTargets = 8;
    RenderTarget *render_target_[kMaxRenderTargets] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    RenderTarget* depth_stencil_target_ = nullptr;

    std::unordered_map<RHIPipeline *, PipelineRenderQueue *> pipeline_render_queues_;
    GlobalUBO global_ubo_data_ = {};
    handle_ty command_buffer_ = 0;
};

}// namespace ocarina

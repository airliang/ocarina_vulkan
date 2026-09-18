//
// Created by Zero on 06/06/2022.
//

#include "renderpass.h"
#include "core/hash.h"
#include "rhi/command_buffer.h"
#include "rhi/pipeline_state.h"
#include "core/logging.h"

namespace ocarina {

RHIRenderPass::RHIRenderPass(const RenderPassCreation &render_pass_creation)
    : render_target_(render_pass_creation.render_target),
      clear_color_(render_pass_creation.clear_color),
      clear_depth_(render_pass_creation.clear_depth),
      clear_stencil_(render_pass_creation.clear_stencil),
      clear_color_attachment_(render_pass_creation.clear_color_attachment),
      clear_depth_attachment_(render_pass_creation.clear_depth_attachment),
      present_swapchain_(render_pass_creation.present_swapchain) {
}

RHIRenderPass::~RHIRenderPass() {
    for (auto &queue : pipeline_render_queues_) {
        ocarina::delete_with_allocator<PipelineRenderQueue>(queue.second);
    }
    pipeline_render_queues_.clear();
}

void RHIRenderPass::clear_draw_call_items() {
    for (auto &queue : pipeline_render_queues_) {
        queue.second->clear();
    }
}

void RHIRenderPass::add_draw_call(uint32_t render_component_index, const PipelineState& pipeline_state) {
    auto it = pipeline_render_queues_.find(pipeline_state);
    if (it != pipeline_render_queues_.end()) {
        it->second->draw_call_items.push_back(render_component_index);
        return;
    }

    PipelineRenderQueue* queue = ocarina::new_with_allocator<PipelineRenderQueue>();
    queue->draw_call_items.push_back(render_component_index);
    pipeline_render_queues_.emplace(pipeline_state, queue);
}

}// namespace ocarina

//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "renderpass.h"
#include "core/hash.h"
#include "rhi/command_buffer.h"
#include "rhi/pipeline_state.h"

namespace ocarina {

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

void RHIRenderPass::add_draw_call(uint32_t render_component_index, RHIPipeline* pipeline) {
    if (pipeline != nullptr)
    {
        auto it = pipeline_render_queues_.find(pipeline);
        if (it != pipeline_render_queues_.end())
        {
            it->second->draw_call_items.push_back(render_component_index);
        } else {
            PipelineRenderQueue *queue = ocarina::new_with_allocator<PipelineRenderQueue>();
            queue->pipeline_line = pipeline;
            queue->draw_call_items.push_back(render_component_index);
            pipeline_render_queues_.insert(std::make_pair(pipeline, queue));
        }
    }
}

}// namespace ocarina

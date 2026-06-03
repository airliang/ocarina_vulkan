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

void RHIRenderPass::add_draw_call(DrawCallItem &item) {
    if (item.pipeline != nullptr)
    {
        auto it = pipeline_render_queues_.find(item.pipeline);
        if (it != pipeline_render_queues_.end())
        {
            it->second->draw_call_items.push_back(item);
        } else {
            PipelineRenderQueue *queue = ocarina::new_with_allocator<PipelineRenderQueue>();
            queue->pipeline_line = item.pipeline;
            queue->draw_call_items.push_back(item);
            pipeline_render_queues_.insert(std::make_pair(item.pipeline, queue));
        }
    }
}

void RHIRenderPass::draw_items(CommandBuffer& cmd) {
    for (const auto& queue : pipeline_render_queues_) {
        cmd.bind_pipeline(queue.first);

        for (auto& item : queue.second->draw_call_items) {
            if (item.pre_render_function) {
                item.pre_render_function(item);
            }
            if (item.push_constant_data) {
                cmd.push_constants(item.push_constant_data, 0, item.push_constant_size);
            }

            if (!item.descriptor_sets.empty())
            {
                cmd.bind_descriptor_sets(item.descriptor_sets.data(), item.first_set, item.descriptor_sets.size(), queue.first->pipeline_layout);
            }
            //for (uint32_t i = 0; i < item.descriptor_set_count; ++i)
            //{
            //    DescriptorSetsBinding& descriptor_set_group = item.descriptor_sets_binding_group[i];
            //    driver.bind_descriptor_sets(descriptor_set_group.descriptor_sets.data(), descriptor_set_group.first_set,
            //        descriptor_set_group.descriptor_set_count, vulkan_pipeline->pipeline_layout_);
            //}

            cmd.set_vertex_buffer(item.vertex_buffer);
            cmd.draw_indexed(item.index_buffer, 1, 0, 0, 0);
            //driver.draw_triangles(cmd_buffer, static_cast<VulkanIndexBuffer *>(item.index_buffer));
        }
    }
}

}// namespace ocarina
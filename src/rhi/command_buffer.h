//
// Created by Zero on 06/07/2022.
//

#pragma once
#include "graphics_descriptions.h"
#include "core/small_vector.h"

namespace ocarina {
class RHIRenderPass;
class DescriptorSet;
struct RHIPipeline;
//This command buffer class is designed to be a holder for the actual command buffer implementation, which can be VulkanCommandBuffer, D3D12CommandBuffer, etc. 
//It also holds the synchronization primitives (semaphores) that are needed for submitting the command buffer to the GPU. 
// The actual command buffer implementation is hidden behind the Impl class, which is a pure virtual interface. 
// This allows us to have a unified CommandBuffer class that can work with different graphics APIs without exposing their specific details to the rest of the codebase.
class CommandBuffer {
public:
    class Impl : public concepts::Noncopyable {
    public:
        Impl() {}
        virtual ~Impl() {}
        virtual void begin_render_pass(RHIRenderPass* render_pass) = 0;
        virtual void end_render_pass() = 0;
        virtual void bind_pipeline(handle_ty pipeline) = 0;
        virtual void bind_descriptor_sets(DescriptorSet** descriptor_sets, uint32_t descriptor_set_count, RHIPipeline* pipeline) = 0;

        SmallVector<Semaphore, MAX_COMMAND_BUFFERS_PER_SUBMIT> wait_semaphores;
        SmallVector<Semaphore, MAX_COMMAND_BUFFERS_PER_SUBMIT> signal_semaphores;
    };
    handle_ty command_buffer = InvalidUI64;

    void reset() const
    {
        if (impl_) {
            impl_->wait_semaphores.clear();
            impl_->signal_semaphores.clear();
        }
    }
    CommandBuffer() = default;
    CommandBuffer(Impl* impl) : impl_(impl) {}

    ~CommandBuffer() { }

    void begin_render_pass(RHIRenderPass* render_pass)
    {
        impl_->begin_render_pass(render_pass);
    }
    void end_render_pass()
    {
        impl_->end_render_pass();
    }
    void bind_pipeline(handle_ty pipeline)
    {
        impl_->bind_pipeline(pipeline);
    }
    void bind_descriptor_sets(DescriptorSet** descriptor_sets, uint32_t descriptor_set_count, RHIPipeline* pipeline)
    {
        impl_->bind_descriptor_sets(descriptor_sets, descriptor_set_count, pipeline);
    }
    void add_wait_semaphore(const Semaphore& semaphore)
    {
        impl_->wait_semaphores.emplace_back(semaphore);
    }
    void add_signal_semaphore(const Semaphore& semaphore)
    {
        impl_->signal_semaphores.emplace_back(semaphore);
    }
    uint32_t wait_semaphore_count() const
    {
        return static_cast<uint32_t>(impl_->wait_semaphores.size());
    }
    uint32_t signal_semaphore_count() const
    {
        return static_cast<uint32_t>(impl_->signal_semaphores.size());
    }
    Semaphore* get_wait_semaphore(uint32_t index)
    {
        if (index >= impl_->wait_semaphores.size()) {
            return nullptr;
        }
        return &impl_->wait_semaphores[index];
    }
    Semaphore* get_signal_semaphore(uint32_t index)
    {
        if (index >= impl_->signal_semaphores.size()) {
            return nullptr;
        }
        return &impl_->signal_semaphores[index];
    }

    Impl* impl() const
    {
        return impl_;
    }

    void draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
    {
        //impl_->draw_indexed(index_count, instance_count, first_index, vertex_offset, first_instance);
    }

    void draw_indirect(handle_ty indirect_buffer, uint32_t draw_count, uint32_t stride)
    {
        //impl_->draw_indirect(indirect_buffer, draw_count, stride);
    }
     void draw_indexed_indirect(handle_ty indirect_buffer, uint32_t draw_count, uint32_t stride)
    {
        //impl_->draw_indexed_indirect(indirect_buffer, draw_count, stride);
     }
private:
    Impl* impl_ = nullptr;
};

}// namespace ocarina
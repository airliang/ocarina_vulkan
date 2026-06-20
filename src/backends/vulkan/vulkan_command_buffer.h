#pragma once

#include "rhi/command_buffer.h"
#include <vulkan/vulkan.h>
namespace ocarina {
class VulkanDevice;
class VulkanPipeline;
class VulkanBuffer;
class VulkanTexture;

struct CommandBufferState {
    VkDescriptorSet bound_sets[MAX_DESCRIPTOR_SETS_PER_SHADER] = { VK_NULL_HANDLE };
    VkPipelineLayout bound_set_layouts[MAX_DESCRIPTOR_SETS_PER_SHADER] = { VK_NULL_HANDLE };
    uint32_t dirty_mask = 0; // Each bit represents a set index (0-MAX_DESCRIPTOR_SETS_PER_SHADER)
};

class VulkanCommandBuffer : public CommandBuffer::Impl {
public:
    VulkanCommandBuffer(VulkanDevice *device, VkCommandPool cmd_pool, VkCommandBuffer cmd_buffer, QueueType queue_type);
    ~VulkanCommandBuffer();

    void begin_render_pass(RHIRenderPass* render_pass) override;
    void end_render_pass() override;
    void bind_pipeline(const RHIPipeline* pipeline) override;
    void bind_descriptor_sets(DescriptorSet** descriptor_sets, uint32_t first_set, uint32_t descriptor_set_count, handle_ty pipeline_layout) override;
    void draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) override;
    void push_constants(const void* data, uint32_t offset, uint32_t size) override;
    void draw_indirect(handle_ty indirect_buffer, uint32_t draw_count, uint32_t stride) override;
    void draw_indexed_indirect(handle_ty indirect_buffer, uint32_t draw_count, uint32_t stride) override;
    void set_vertex_buffer(VertexBuffer* vertex_buffer, uint32_t base_vertex = 0) override;
    void set_index_buffer(IndexBuffer* index_buffer, uint32_t first_index = 0) override;
    void submit_to_queue(QueueType queue_type, Fence* fence) override;
    void begin() override;
    void end() override;
    OC_MAKE_MEMBER_GETTER(vulkan_command_buffer, )
    //OC_MAKE_MEMBER_GETTER_SETTER(pipeline_stage_flags, )
    VkPipelineStageFlags2 pipeline_stage_flags() const { return pipeline_stage_flags_; }
    void set_pipeline_stage_flags(VkPipelineStageFlags2 flags) { pipeline_stage_flags_ = flags; }
    void reset();
    void copy_buffer(VulkanBuffer* src, VulkanBuffer* dst);
    void copy_image(VulkanBuffer* src, VulkanTexture* dst);
    /// Records vkCmdPipelineBarrier for a color image layout transition (subset of layouts used by texture upload).
    void image_layout_barrier(VulkanTexture* texture, VkImageLayout old_layout, VkImageLayout new_layout);
    QueueType queue_type() const noexcept { return queue_type_; }
private:
    VulkanDevice *device_ = nullptr;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer vulkan_command_buffer_ = VK_NULL_HANDLE;
    VkPipelineStageFlags2 pipeline_stage_flags_; 
    VulkanPipeline* current_pipeline_ = nullptr;
    CommandBufferState state_;
    QueueType queue_type_ = QueueType::Graphics;
};

}// namespace ocarina
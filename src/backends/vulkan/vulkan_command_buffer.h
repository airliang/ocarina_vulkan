#pragma once

#include "rhi/command_buffer.h"
#include <vulkan/vulkan.h>
namespace ocarina {
class VulkanDevice;


class VulkanCommandBuffer : public CommandBuffer::Impl {
public:
    VulkanCommandBuffer(VulkanDevice *device, VkCommandPool cmd_pool, VkCommandBuffer cmd_buffer);
    ~VulkanCommandBuffer();

    void begin_render_pass(RHIRenderPass* render_pass) override;
    void end_render_pass() override;
    void bind_pipeline(handle_ty pipeline) override;
    void bind_descriptor_sets(DescriptorSet** descriptor_sets, uint32_t descriptor_set_count, RHIPipeline* pipeline) override;
    OC_MAKE_MEMBER_GETTER(vulkan_command_buffer, )
    //OC_MAKE_MEMBER_GETTER_SETTER(pipeline_stage_flags, )
    VkPipelineStageFlags2 pipeline_stage_flags() const { return pipeline_stage_flags_; }
    void set_pipeline_stage_flags(VkPipelineStageFlags2 flags) { pipeline_stage_flags_ = flags; }
private:
    VulkanDevice *device_ = nullptr;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer vulkan_command_buffer_ = VK_NULL_HANDLE;
    VkPipelineStageFlags2 pipeline_stage_flags_;
};

}// namespace ocarina
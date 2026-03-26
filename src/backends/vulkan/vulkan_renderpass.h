//
// Created by Zero on 06/08/2022.
//

#pragma once

#include "rhi/renderpass.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanDevice;
class CommandBuffer;

class VulkanRenderPass : public RHIRenderPass {
public:
    VulkanRenderPass(VulkanDevice *device, const RenderPassCreation &render_pass_creation);
    ~VulkanRenderPass() override;

    void begin_render_pass(const CommandBuffer& cmd) override;

    void end_render_pass(const CommandBuffer& cmd) override;
    OC_MAKE_MEMBER_GETTER(render_pass, )
    OC_MAKE_MEMBER_GETTER(clear_values, )
    OC_MAKE_MEMBER_GETTER(vulkan_frame_buffer, )
    void draw_items(const CommandBuffer& cmd) override;

private:
    void setup_render_pass();
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VulkanDevice *device_ = nullptr;
    VkRenderPassBeginInfo render_pass_begin_info_ = {};
    VkClearValue clear_values_[kMaxRenderTargets + 1];
    VkFramebuffer vulkan_frame_buffer_ = VK_NULL_HANDLE;
};
}// namespace ocarina
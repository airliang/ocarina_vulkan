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

    OC_MAKE_MEMBER_GETTER(render_pass, )
    OC_MAKE_MEMBER_GETTER(clear_values, )
    OC_MAKE_MEMBER_GETTER(color_attachment_formats, )
    OC_MAKE_MEMBER_GETTER(color_attachment_format_count, )
    OC_MAKE_MEMBER_GETTER(depth_attachment_format, )

private:
    void setup_render_pass();
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VulkanDevice *device_ = nullptr;
    VkClearValue clear_values_[kMaxColorAttachments + 1];
    VkFormat color_attachment_formats_[kMaxColorAttachments] = {};
    uint32_t color_attachment_format_count_ = 0;
    VkFormat depth_attachment_format_ = VK_FORMAT_UNDEFINED;
};
}// namespace ocarina

//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "graphics_descriptions.h"
#include "device.h"

namespace ocarina {

struct OC_RHI_API ImguiCreation {
    handle_ty instance_;
    handle_ty physical_device_;
    handle_ty device_;
    handle_ty queue_;
    uint32_t queue_family_;
    uint32_t image_count_;
    handle_ty descriptor_pool_;
    handle_ty swapchain_render_pass_;
    handle_ty pipeline_cache_;
    handle_ty surface_;
    void* check_VkResult_fn_;
    void* allocator_callback_;
    /// When true, ImGui uses dynamic rendering (Vulkan 1.3+) instead of VkRenderPass.
    bool use_dynamic_rendering_ = false;
    /// VkFormat values used when use_dynamic_rendering_ is true.
    uint32_t color_attachment_format_ = 0;
    uint32_t depth_attachment_format_ = 0;
    uint32_t api_version_ = 0;
};

}// namespace ocarina
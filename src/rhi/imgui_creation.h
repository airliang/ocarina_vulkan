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
};

struct OC_RHI_API ImguiFrameInfo {
    handle_ty command_buffer_;
    handle_ty render_pass_;
    handle_ty framebuffer_;
    uint2 framebuffer_size_;
    float4 clear_color_;
    float clear_depth_;
    uint32_t clear_stencil_;
};

}// namespace ocarina
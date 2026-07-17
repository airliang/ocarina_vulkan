#include "vulkan_driver.h"
#include "rhi/bindless_sampler.h"
#include "vulkan_device.h"
#include "util.h"
#include "vulkan_pipeline.h"
#include "rhi/context.h"
#include "rhi/graphics_descriptions.h"
#include "vulkan_shader.h"
#include "vulkan_descriptorset.h"
#include "vulkan_renderpass.h"
#include "vulkan_vertex_buffer.h"
#include "vulkan_index_buffer.h"
#include "vulkan_texture.h"
#include "rhi/resources/texture_sampler.h"
#include "vulkan_command_buffer.h"

namespace ocarina {

VulkanDriver::VulkanDriver() {
    
}

VulkanDriver::~VulkanDriver() {
   
}

VulkanDevice* VulkanDriver::create_device(RHIContext* file_manager, const InstanceCreation& instance_creation)
{
    vulkan_device_ = ocarina::new_with_allocator<ocarina::VulkanDevice>(file_manager, instance_creation);

    initialize();

    return vulkan_device_;
}

void VulkanDriver::bind_pipeline(VkCommandBuffer cmd_buffer, const VulkanPipeline &pipeline) {
    //VkCommandBuffer current_buffer = get_current_command_buffer();

    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline_);
}

void VulkanDriver::terminate()
{
    release_frame_buffer();

    for (auto &it : render_passes_) {
        ocarina::delete_with_allocator(it);
    }
    render_passes_.clear();

    vulkan_descriptor_manager->clear();
    vulkan_shader_manager->clear(vulkan_device_);
    destroy_frame_sync();
    release_command_buffers();
    release_command_pool();

    destroy_internal_textures();

    if (gpu_timestamp_query_pool_ != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device(), gpu_timestamp_query_pool_, nullptr);
        gpu_timestamp_query_pool_ = VK_NULL_HANDLE;
    }
    gpu_timestamp_written_.clear();

    for (auto& it : samplers_)
    {
        vkDestroySampler(device(), it.second, nullptr);
    }
    samplers_.clear();
    //if (empty_descriptorset_layout_ != VK_NULL_HANDLE) {
    //    vkDestroyDescriptorSetLayout(device(), empty_descriptorset_layout_, nullptr);
    //    empty_descriptorset_layout_ = VK_NULL_HANDLE;
    //}
    if (imgui_descriptorpool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device(), imgui_descriptorpool_, nullptr);
        imgui_descriptorpool_ = VK_NULL_HANDLE;
    }
}

void VulkanDriver::submit_command_buffer(VkCommandBuffer cmd_buffer) {
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd_buffer;
    VK_CHECK_RESULT(vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
    //VK_CHECK_RESULT(vkQueueWaitIdle(graphics_queue));
}

inline VkDevice VulkanDriver::device() const {
    return (*vulkan_device_)();
}

VulkanShader *VulkanDriver::create_shader(ShaderType shader_type,
                                          const std::string &filename,
                                          const std::set<std::string> &options,
                                          const std::string &entry_point){
    return vulkan_shader_manager->get_or_create_from_HLSL(vulkan_device_, shader_type, filename, options, entry_point);
}

VulkanShader* VulkanDriver::get_shader(handle_ty shader) const
{
    return vulkan_shader_manager->get_shader(shader);
}

void VulkanDriver::setup_frame_buffer()
{
    if (vulkan_device_->supports_dynamic_rendering()) {
        renderpass_framebuffer = VK_NULL_HANDLE;
        frame_buffers.clear();
        return;
    }

    VulkanSwapchain *swapchain = vulkan_device_->get_swapchain();
    uint2 resolution = swapchain->resolution();

    //first create the frame buffer attach renderpass
    std::array<VkAttachmentDescription, 2> attachments = {};
    // Color attachment
    attachments[0].format = swapchain->color_format();
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    // Depth attachment
    VkFormat depth_format = swapchain->depth_format();
    attachments[1].format = depth_format;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference = {};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference = {};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassDescription = {};
    subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDescription.colorAttachmentCount = 1;
    subpassDescription.pColorAttachments = &colorReference;
    subpassDescription.pDepthStencilAttachment = &depthReference;
    subpassDescription.inputAttachmentCount = 0;
    subpassDescription.pInputAttachments = nullptr;
    subpassDescription.preserveAttachmentCount = 0;
    subpassDescription.pPreserveAttachments = nullptr;
    subpassDescription.pResolveAttachments = nullptr;

    // Subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 2> dependencies;

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    dependencies[0].dependencyFlags = 0;

    dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].dstSubpass = 0;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = 0;
    dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    dependencies[1].dependencyFlags = 0;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpassDescription;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    VkResult err = vkCreateRenderPass(device(), &renderPassInfo, nullptr, &renderpass_framebuffer);
    VK_CHECK_RESULT(err);

    //create framebuffer
    VkImageView imageview_attachments[2];

    VulkanSwapchain::DepthStencil depth_stencil = swapchain->get_depth_stencil();

    // Depth/Stencil attachment is the same for all frame buffers
    imageview_attachments[1] = depth_stencil.view;

    VkFramebufferCreateInfo frameBufferCreateInfo = {};
    frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    frameBufferCreateInfo.pNext = NULL;
    frameBufferCreateInfo.renderPass = renderpass_framebuffer;
    frameBufferCreateInfo.attachmentCount = 2;
    frameBufferCreateInfo.pAttachments = imageview_attachments;
    frameBufferCreateInfo.width = resolution.x;
    frameBufferCreateInfo.height = resolution.y;
    frameBufferCreateInfo.layers = 1;

    // Create frame buffers for every swap chain image
    frame_buffers.resize(swapchain->backbuffer_size());
    for (uint32_t i = 0; i < frame_buffers.size(); i++) {
        imageview_attachments[0] = swapchain->get_swapchain_buffer(i).imageView_;
        VK_CHECK_RESULT(vkCreateFramebuffer(device(), &frameBufferCreateInfo, nullptr, &frame_buffers[i]));
    }
}

/*
void VulkanDriver::setup_depth_stencil(uint32_t width, uint32_t height) {
    get_supported_depth_format(vulkan_device_->physicalDevice(), &depth_stencil_format);
    VkImageCreateInfo imageCI{};
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = depth_stencil_format;
    imageCI.extent = {width, height, 1};
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 1;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VK_CHECK_RESULT(vkCreateImage(device(), &imageCI, nullptr, &depth_stencil.image));
    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(device(), depth_stencil.image, &memReqs);

    VkMemoryAllocateInfo memAllloc{};
    memAllloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllloc.allocationSize = memReqs.size;
    memAllloc.memoryTypeIndex = vulkan_device_->get_memory_type(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(device(), &memAllloc, nullptr, &depth_stencil.mem));
    VK_CHECK_RESULT(vkBindImageMemory(device(), depth_stencil.image, depth_stencil.mem, 0));

    VkImageViewCreateInfo imageViewCI{};
    imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCI.image = depth_stencil.image;
    imageViewCI.format = depth_stencil_format;
    imageViewCI.subresourceRange.baseMipLevel = 0;
    imageViewCI.subresourceRange.levelCount = 1;
    imageViewCI.subresourceRange.baseArrayLayer = 0;
    imageViewCI.subresourceRange.layerCount = 1;
    imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    // Stencil aspect should only be set on depth + stencil formats (VK_FORMAT_D16_UNORM_S8_UINT..VK_FORMAT_D32_SFLOAT_S8_UINT
    if (depth_stencil_format >= VK_FORMAT_D16_UNORM_S8_UINT) {
        imageViewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    VK_CHECK_RESULT(vkCreateImageView(device(), &imageViewCI, nullptr, &depth_stencil.view));
}
*/

void VulkanDriver::release_frame_buffer()
{
    if (renderpass_framebuffer != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device(), renderpass_framebuffer, nullptr);
        renderpass_framebuffer = VK_NULL_HANDLE;
    }
    for (uint32_t i = 0; i < frame_buffers.size(); i++) {
        if (frame_buffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device(), frame_buffers[i], nullptr);
            frame_buffers[i] = VK_NULL_HANDLE;
        }
    }
    frame_buffers.clear();
}

void VulkanDriver::create_command_pool()
{
    for (size_t i = 0; i < (size_t)QueueType::NumQueueType; ++i) {
        command_pools_[i] = VK_NULL_HANDLE;
    }

    for (QueueType queue_type : { QueueType::Graphics, QueueType::Compute, QueueType::Copy }) {
        const uint32_t family_index = vulkan_device_->get_queue_family_index(queue_type);
        VkCommandPoolCreateInfo cmd_pool_info{};
        cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmd_pool_info.queueFamilyIndex = family_index;
        cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK_RESULT(vkCreateCommandPool(device(), &cmd_pool_info, nullptr, &command_pools_[(size_t)queue_type]));
    }
}

void VulkanDriver::release_command_pool()
{
    for (size_t i = 0; i < (size_t)QueueType::NumQueueType; ++i) {
        if (command_pools_[i] != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device(), command_pools_[i], nullptr);
            command_pools_[i] = VK_NULL_HANDLE;
        }
    }
}

void VulkanDriver::create_command_buffers()
{
    VulkanSwapchain *swapchain = vulkan_device_->get_swapchain();
    frames_in_flight_ = std::min({kMaxFramesInFlight, swapchain->backbuffer_size()});
    if (frames_in_flight_ == 0) {
        frames_in_flight_ = 1;
    }
    command_buffer_pools_.resize(frames_in_flight_);
    create_frame_sync();
}

void VulkanDriver::release_command_buffers() {
    for (auto& per_queue_pools : command_buffer_pools_) {
        for (size_t q = 0; q < (size_t)QueueType::NumQueueType; ++q) {
            auto& pool = per_queue_pools[q];
            while (!pool.empty()) {
                auto cmd_buffer = pool.front();
                ocarina::delete_with_allocator<VulkanCommandBuffer>(cmd_buffer);
                pool.pop();
            }
        }
    }
    command_buffer_pools_.clear();
}

void VulkanDriver::initialize()
{
    vulkan_shader_manager = std::make_unique<VulkanShaderManager>();
    vulkan_descriptor_manager = std::make_unique<VulkanDescriptorManager>(vulkan_device_);
    setup_frame_buffer();
    //get the graphics queue
    vkGetDeviceQueue(device(), vulkan_device_->get_queue_family_index(QueueType::Graphics), 0, &graphics_queue);
    vkGetDeviceQueue(device(), vulkan_device_->get_queue_family_index(QueueType::Compute), 0, &compute_queue);
    vkGetDeviceQueue(device(), vulkan_device_->get_queue_family_index(QueueType::Copy), 0, &copy_queue);
    create_command_pool();
    create_command_buffers();

    create_internal_textures();

    // GPU timestamp queries (2 per in-flight frame).
    if (gpu_timestamp_query_pool_ == VK_NULL_HANDLE && frames_in_flight_ > 0) {
        VkQueryPoolCreateInfo query_info{};
        query_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query_info.queryCount = frames_in_flight_ * 2;
        VK_CHECK_RESULT(vkCreateQueryPool(device(), &query_info, nullptr, &gpu_timestamp_query_pool_));
        gpu_timestamp_period_ns_ = static_cast<double>(vulkan_device_->device_limits().timestampPeriod);
        gpu_frame_time_ms_ = 0.0;
        gpu_timestamp_written_.assign(frames_in_flight_, 0u);
    }
}

void VulkanDriver::create_frame_sync()
{
    destroy_frame_sync();

    frame_sync_.resize(frames_in_flight_);
    VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

#if OCARINA_VULKAN_FRAME_SYNC_TIMELINE
    if (frame_timeline_sync_.timeline_semaphore == VK_NULL_HANDLE) {
        VkSemaphoreTypeCreateInfo timeline_type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        timeline_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timeline_type.initialValue = 0;
        semaphore_info.pNext = &timeline_type;
        VK_CHECK_RESULT(vkCreateSemaphore(device(), &semaphore_info, nullptr, &frame_timeline_sync_.timeline_semaphore));
        semaphore_info.pNext = nullptr;
    }
    frame_timeline_sync_.frame_number = 0;
#else
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
#endif

    for (uint32_t i = 0; i < frames_in_flight_; ++i) {
        VK_CHECK_RESULT(vkCreateSemaphore(device(), &semaphore_info, nullptr, &frame_sync_[i].image_available));
        VK_CHECK_RESULT(vkCreateSemaphore(device(), &semaphore_info, nullptr, &frame_sync_[i].render_finished));
#if OCARINA_VULKAN_FRAME_SYNC_TIMELINE
        frame_sync_[i].in_flight_fence = VK_NULL_HANDLE;
#else
        VK_CHECK_RESULT(vkCreateFence(device(), &fence_info, nullptr, &frame_sync_[i].in_flight_fence));
#endif
    }
    current_frame_ = 0;
}

void VulkanDriver::destroy_frame_sync()
{
    if (frame_sync_.empty()) {
        return;
    }

    if (device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device());
    }

#if OCARINA_VULKAN_FRAME_SYNC_TIMELINE
    if (frame_timeline_sync_.timeline_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device(), frame_timeline_sync_.timeline_semaphore, nullptr);
        frame_timeline_sync_.timeline_semaphore = VK_NULL_HANDLE;
    }
    frame_timeline_sync_.frame_number = 0;
#endif

    for (auto& frame : frame_sync_) {
#if !OCARINA_VULKAN_FRAME_SYNC_TIMELINE
        if (frame.in_flight_fence != VK_NULL_HANDLE) {
            vkDestroyFence(device(), frame.in_flight_fence, nullptr);
            frame.in_flight_fence = VK_NULL_HANDLE;
        }
#endif
        if (frame.image_available != VK_NULL_HANDLE) {
            vkDestroySemaphore(device(), frame.image_available, nullptr);
            frame.image_available = VK_NULL_HANDLE;
        }
        if (frame.render_finished != VK_NULL_HANDLE) {
            vkDestroySemaphore(device(), frame.render_finished, nullptr);
            frame.render_finished = VK_NULL_HANDLE;
        }
    }
    frame_sync_.clear();
}

void VulkanDriver::destroy_swapchain_framebuffers()
{
    for (uint32_t i = 0; i < frame_buffers.size(); i++) {
        if (frame_buffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device(), frame_buffers[i], nullptr);
            frame_buffers[i] = VK_NULL_HANDLE;
        }
    }
    frame_buffers.clear();
}

void VulkanDriver::create_swapchain_framebuffers()
{
    if (vulkan_device_->supports_dynamic_rendering()) {
        frame_buffers.clear();
        return;
    }

    VulkanSwapchain* swapchain = vulkan_device_->get_swapchain();
    if (!swapchain->is_valid() || renderpass_framebuffer == VK_NULL_HANDLE) {
        return;
    }

    const uint2 resolution = swapchain->resolution();
    VkImageView imageview_attachments[2];
    VulkanSwapchain::DepthStencil depth_stencil = swapchain->get_depth_stencil();
    imageview_attachments[1] = depth_stencil.view;

    VkFramebufferCreateInfo frameBufferCreateInfo = {};
    frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    frameBufferCreateInfo.pNext = NULL;
    frameBufferCreateInfo.renderPass = renderpass_framebuffer;
    frameBufferCreateInfo.attachmentCount = 2;
    frameBufferCreateInfo.pAttachments = imageview_attachments;
    frameBufferCreateInfo.width = resolution.x;
    frameBufferCreateInfo.height = resolution.y;
    frameBufferCreateInfo.layers = 1;

    frame_buffers.resize(swapchain->backbuffer_size());
    for (uint32_t i = 0; i < frame_buffers.size(); i++) {
        imageview_attachments[0] = swapchain->get_swapchain_buffer(i).imageView_;
        VK_CHECK_RESULT(vkCreateFramebuffer(device(), &frameBufferCreateInfo, nullptr, &frame_buffers[i]));
    }
}

void VulkanDriver::update_swapchain_render_pass_extents()
{
    VulkanSwapchain* swapchain = vulkan_device_->get_swapchain();
    if (swapchain == nullptr || !swapchain->is_valid()) {
        return;
    }

    const uint2 extent = swapchain->resolution();
    for (VulkanRenderPass* render_pass : render_passes_) {
        if (render_pass != nullptr) {
            render_pass->update_swapchain_extent(extent);
        }
    }
}

bool VulkanDriver::recreate_swapchain()
{
    vulkan_device_->wait_idle();

    destroy_swapchain_framebuffers();

    VulkanSwapchain* swapchain = vulkan_device_->get_swapchain();
    uint32_t width = 0;
    uint32_t height = 0;
    (void)swapchain->query_surface_extent(width, height);

    const bool ok = swapchain->recreate_swapchain(width, height);
    if (!ok) {
        frame_acquired_ = false;
        return false;
    }

    create_swapchain_framebuffers();
    update_swapchain_render_pass_extents();
    return true;
}

bool VulkanDriver::begin_frame()
{
    frame_acquired_ = false;

    VulkanSwapchain* swapchain = vulkan_device_->get_swapchain();
    if (!swapchain->is_valid()) {
        if (!recreate_swapchain()) {
            return false;
        }
    }

#if OCARINA_VULKAN_FRAME_SYNC_TIMELINE
    // Wait for an older in-flight frame before acquiring. Frame number is
    // incremented only after a successful acquire so skipped frames cannot
    // leave the timeline semaphore unsignaled.
    const uint64_t upcoming_frame = frame_timeline_sync_.frame_number + 1;
    if (frame_timeline_sync_.timeline_semaphore != VK_NULL_HANDLE &&
        upcoming_frame > frames_in_flight_) {
        const uint64_t wait_value = upcoming_frame - frames_in_flight_;
        VkSemaphoreWaitInfo wait_info{};
        wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wait_info.semaphoreCount = 1;
        wait_info.pSemaphores = &frame_timeline_sync_.timeline_semaphore;
        wait_info.pValues = &wait_value;
        VK_CHECK_RESULT(vkWaitSemaphores(device(), &wait_info, UINT64_MAX));
    }
    const uint32_t slot = static_cast<uint32_t>(upcoming_frame % frames_in_flight_);
#else
    FrameSync& frame = frame_sync_[current_frame_];
    VK_CHECK_RESULT(vkWaitForFences(device(), 1, &frame.in_flight_fence, VK_TRUE, UINT64_MAX));
    const uint32_t slot = current_frame_;
#endif

    // Resolve GPU timestamp for the last time we used this frame slot.
    if (gpu_timestamp_query_pool_ != VK_NULL_HANDLE) {
        const uint32_t query_base = slot * 2;
        if (slot < gpu_timestamp_written_.size() && gpu_timestamp_written_[slot] != 0u) {
            uint64_t timestamps[2] = {0, 0};
            const VkResult qr = vkGetQueryPoolResults(
                device(),
                gpu_timestamp_query_pool_,
                query_base,
                2,
                sizeof(timestamps),
                timestamps,
                sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
            if (qr == VK_SUCCESS) {
                const uint64_t delta = (timestamps[1] > timestamps[0]) ? (timestamps[1] - timestamps[0]) : 0;
                const double ns = static_cast<double>(delta) * gpu_timestamp_period_ns_;
                gpu_frame_time_ms_ = ns * 1e-6;
            }
            gpu_timestamp_written_[slot] = 0u;
        }
    }

    VkResult result = swapchain->aquire_next_image(frame_sync_[slot].image_available, &current_buffer_);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        if (!recreate_swapchain()) {
            return false;
        }
        // Skip this frame after recreation; next frame will acquire cleanly.
        return false;
    }
    if (result != VK_SUCCESS) {
        VK_CHECK_RESULT(result);
        return false;
    }

#if OCARINA_VULKAN_FRAME_SYNC_TIMELINE
    ++frame_timeline_sync_.frame_number;
#else
    VK_CHECK_RESULT(vkResetFences(device(), 1, &frame_sync_[current_frame_].in_flight_fence));
#endif
    frame_acquired_ = true;
    return true;
}

void VulkanDriver::write_gpu_timestamp_begin(VkCommandBuffer cmd) noexcept {
    if (gpu_timestamp_query_pool_ == VK_NULL_HANDLE) {
        return;
    }
    const uint32_t query_base = frame_slot() * 2;
    // Reset both queries for this frame slot before writing timestamps (no hostQueryReset needed).
    vkCmdResetQueryPool(cmd, gpu_timestamp_query_pool_, query_base, 2);
    const uint32_t query = query_base + 0;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, gpu_timestamp_query_pool_, query);
}

void VulkanDriver::write_gpu_timestamp_end(VkCommandBuffer cmd) noexcept {
    if (gpu_timestamp_query_pool_ == VK_NULL_HANDLE) {
        return;
    }
    const uint32_t slot = frame_slot();
    const uint32_t query = slot * 2 + 1;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, gpu_timestamp_query_pool_, query);
    if (slot < gpu_timestamp_written_.size()) {
        gpu_timestamp_written_[slot] = 1u;
    }
}

void VulkanDriver::end_frame()
{
    if (!frame_acquired_) {
#if !OCARINA_VULKAN_FRAME_SYNC_TIMELINE
        current_frame_ = (current_frame_ + 1) % frames_in_flight_;
#endif
        return;
    }

    const VkResult result = vulkan_device_->get_swapchain()->queue_present(
        graphics_queue, current_buffer_, frame_sync_[frame_slot()].render_finished);
    frame_acquired_ = false;

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        (void)recreate_swapchain();
    } else if (result != VK_SUCCESS) {
        VK_CHECK_RESULT(result);
    }

#if !OCARINA_VULKAN_FRAME_SYNC_TIMELINE
    current_frame_ = (current_frame_ + 1) % frames_in_flight_;
#endif
}

void VulkanDriver::flush_command_buffer(VkCommandBuffer cmd)
{
    // Create fence to ensure that the command buffer has finished executing
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = 0;
    fence_info.pNext = nullptr;
    VkFence fence;
    VK_CHECK_RESULT(vkCreateFence(device(), &fence_info, nullptr, &fence));
    // Submit to the queue
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    VK_CHECK_RESULT(vkQueueSubmit(graphics_queue, 1, &submitInfo, fence));
    // Wait for the fence to signal that command buffer has finished executing
    VK_CHECK_RESULT(vkWaitForFences(device(), 1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max()));
    vkDestroyFence(device(), fence, nullptr);
}

VkSampler VulkanDriver::get_vulkan_sampler(const TextureSampler& sampler) {
    const Hashable* hash = reinterpret_cast<const Hashable*>(&sampler);
    auto it = samplers_.find(hash->hash());
    if (it != samplers_.end()) {
        return it->second;
    }

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = get_vulkan_filter(sampler.filter());
    sampler_info.minFilter = get_vulkan_filter(sampler.filter());
    sampler_info.mipmapMode = get_vulkan_sampler_mipmap_mode(sampler.mipmap_filter());
    sampler_info.addressModeU = get_vulkan_sampler_address(sampler.u_address());
    sampler_info.addressModeV = get_vulkan_sampler_address(sampler.v_address());
    sampler_info.addressModeW = get_vulkan_sampler_address(sampler.w_address());
    sampler_info.mipLodBias = 0.0f;
    sampler_info.compareOp = VK_COMPARE_OP_NEVER;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = VK_LOD_CLAMP_NONE;
    sampler_info.maxAnisotropy = 1.0f;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    VkSampler vk_sampler;
    VK_CHECK_RESULT(vkCreateSampler(device(), &sampler_info, nullptr, &vk_sampler));

    samplers_.insert(std::make_pair(hash->hash(), vk_sampler));
    return vk_sampler;
}

VkSampler VulkanDriver::get_bindless_sampler(uint32_t index) {
    return get_vulkan_sampler(bindless_sampler_config(index));
}

uint32_t VulkanDriver::frame_slot() const noexcept
{
#if OCARINA_VULKAN_FRAME_SYNC_TIMELINE
    return static_cast<uint32_t>(frame_timeline_sync_.frame_number % frames_in_flight_);
#else
    return current_frame_;
#endif
}

uint32_t VulkanDriver::current_frame() const noexcept
{
    return frame_slot();
}

VkSemaphore VulkanDriver::get_present_complete_semaphore() const
{
    return frame_sync_[frame_slot()].image_available;
}

VkSemaphore VulkanDriver::get_render_complete_semaphore() const
{
    return frame_sync_[frame_slot()].render_finished;
}

VulkanRenderPass* VulkanDriver::create_render_pass(const RenderPassCreation& render_pass_creation) {
    VulkanRenderPass* render_pass = ocarina::new_with_allocator<VulkanRenderPass>(vulkan_device_, render_pass_creation);
    render_passes_.push_back(render_pass);
    return render_pass;
}

void VulkanDriver::destroy_render_pass(VulkanRenderPass* render_pass) {
    auto it = std::find(render_passes_.begin(), render_passes_.end(), render_pass);
    if (it != render_passes_.end()) {
        render_passes_.erase(it);
    }
    ocarina::delete_with_allocator<VulkanRenderPass>(render_pass);
}

void VulkanDriver::draw_triangles(VkCommandBuffer cmd, VulkanIndexBuffer* index_buffer) {
    //VkCommandBuffer current_buffer = get_current_command_buffer();
    vkCmdBindIndexBuffer(cmd, index_buffer->buffer_handle(), 0, index_buffer->is_16_bit() ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, index_buffer->get_index_count(), 1, 0, 0, 0);
}

void VulkanDriver::push_constants(VkCommandBuffer cmd, VkPipelineLayout pipeline_layout, const void *data, uint32_t size, uint32_t offset, VkShaderStageFlags stage_flags) {
    //VkCommandBuffer current_buffer = get_current_command_buffer();
    vkCmdPushConstants(cmd, pipeline_layout, stage_flags, offset, size, data);
}

void VulkanDriver::bind_descriptor_sets(VkCommandBuffer cmd, DescriptorSet **descriptor_sets, uint32_t first_set, uint32_t descriptor_sets_num, VkPipelineLayout pipeline_layout) {
    //VkCommandBuffer current_buffer = get_current_command_buffer();
    std::array<VkDescriptorSet, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_handles = {VK_NULL_HANDLE};
    //uint32_t first_set = -1;
    for (uint32_t i = 0; i < descriptor_sets_num; ++i) {
        //if (descriptor_sets[i] == nullptr) {
        //    continue;
        //}
        //if (first_set == -1) {
        //    first_set = descriptor_sets[i]->get_layout()->get_descriptor_set_index();
        //}
        descriptor_set_handles[i] = static_cast<VulkanDescriptorSet*>(descriptor_sets[i])->descriptor_set();
    }
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, first_set, descriptor_sets_num, descriptor_set_handles.data(), 0, nullptr);
}

std::array<DescriptorSetLayout *, MAX_DESCRIPTOR_SETS_PER_SHADER> VulkanDriver::create_descriptor_set_layout(VulkanShader *shaders[], uint32_t shaders_count) {
    return vulkan_descriptor_manager->create_descriptor_set_layout(shaders, shaders_count);
}

//VkPipelineLayout VulkanDriver::get_pipeline_layout(VkDescriptorSetLayout *descriptset_layouts, uint8_t descriptset_layouts_count, VkPushConstantRange *push_constants, uint32_t push_constant_array_size) {
//    return vulkan_pipeline_manager->get_pipeline_layout(vulkan_device_, descriptset_layouts, descriptset_layouts_count, push_constant_size);
//}

void VulkanDriver::create_internal_textures() {
    if (internal_textures_[INTERNAL_TEXTURE_WHITE] == nullptr)
    {
        TextureViewCreation texture_view = {};
        texture_view.mip_level_count = 1;
        texture_view.usage = TextureUsageFlags::ShaderReadOnly;
        TextureSampler sampler = {TextureSampler::Filter::LINEAR_LINEAR, TextureSampler::Address::REPEAT};
        internal_textures_[INTERNAL_TEXTURE_WHITE] = ocarina::new_with_allocator<VulkanTexture>(
            vulkan_device_, 4, 4, 1, PixelStorage::BYTE4, texture_view, sampler, uint4(255, 255, 255, 255), nullptr);
    }
}

void VulkanDriver::destroy_internal_textures()
{
    for (size_t i = 0; i < INTERNAL_TEXTURE_COUNT; ++i) {
        if (internal_textures_[i] != nullptr) {
            ocarina::delete_with_allocator<VulkanTexture>(internal_textures_[i]);
            internal_textures_[i] = nullptr;
        }
    }
}

VkDescriptorPool VulkanDriver::get_imgui_descriptor_pool()
{
    if (imgui_descriptorpool_ != VK_NULL_HANDLE) {
        return imgui_descriptorpool_;
    }
    VkDescriptorPoolSize pool_sizes[] =
    {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8 },
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 0;
    for (VkDescriptorPoolSize& pool_size : pool_sizes)
        pool_info.maxSets += pool_size.descriptorCount;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = pool_sizes;
    VK_CHECK_RESULT(vkCreateDescriptorPool(device(), &pool_info, nullptr, &imgui_descriptorpool_));

    return imgui_descriptorpool_;
}

VulkanCommandBuffer* VulkanDriver::get_command_buffer(QueueType queue_type) {
    std::lock_guard<std::mutex> lock(command_buffer_pool_mutex_);

    const uint32_t slot = frame_slot();
    auto& pool = command_buffer_pools_[slot][(size_t)queue_type];
    if (pool.empty()) {
        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = command_pools_[(size_t)queue_type];
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        VkCommandBuffer cmd_buffer = VK_NULL_HANDLE;
        VK_CHECK_RESULT(vkAllocateCommandBuffers(device(), &allocateInfo, &cmd_buffer));
        return ocarina::new_with_allocator<ocarina::VulkanCommandBuffer>(vulkan_device_, command_pools_[(size_t)queue_type], cmd_buffer, queue_type);
    }

    VulkanCommandBuffer* cmd_buffer = pool.front();
    pool.pop();
    return cmd_buffer;
}

void VulkanDriver::release_command_buffer(VulkanCommandBuffer* cmd_buffer)
{
    std::lock_guard<std::mutex> lock(command_buffer_pool_mutex_);
    cmd_buffer->reset();
    command_buffer_pools_[frame_slot()][(size_t)cmd_buffer->queue_type()].push(cmd_buffer);
}

void VulkanDriver::queue_submit(VkQueue queue, const VkSubmitInfo2* submit_info, uint32_t submit_count, VkFence fence)
{
    VK_CHECK_RESULT(vkQueueSubmit2(queue, submit_count, submit_info, fence));
}

void VulkanDriver::execute_command_buffers(CommandBuffer* cmd_buffers, uint32_t counts, VkFence fence)
{
#if !OCARINA_VULKAN_FRAME_SYNC_TIMELINE
    VkFence frame_fence = VK_NULL_HANDLE;
    if (fence == VK_NULL_HANDLE && !frame_sync_.empty()) {
        frame_fence = frame_sync_[current_frame_].in_flight_fence;
    }
#endif

    std::array<VkCommandBufferSubmitInfo, MAX_COMMAND_BUFFERS_PER_SUBMIT> cmd_buffers_submit{};
    std::array<VkSemaphoreSubmitInfo, MAX_COMMAND_BUFFERS_PER_SUBMIT> signals{};
    std::array<VkSemaphoreSubmitInfo, MAX_COMMAND_BUFFERS_PER_SUBMIT> waits{};

    for (uint32_t i = 0; i < counts; ++i) {
        cmd_buffers_submit[i].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmd_buffers_submit[i].commandBuffer = reinterpret_cast<VkCommandBuffer>(cmd_buffers[i].command_buffer);
    }

    for (uint32_t i = 0; i < counts; ++i) {
        VulkanCommandBuffer* vulkan_cmd_buffer = static_cast<VulkanCommandBuffer*>(cmd_buffers[i].impl());
        const bool is_last_submit = (i + 1 == counts);

#if OCARINA_VULKAN_FRAME_SYNC_TIMELINE
        const bool signal_frame_timeline =
            fence == VK_NULL_HANDLE && is_last_submit && frame_timeline_sync_.timeline_semaphore != VK_NULL_HANDLE;
#endif

        uint32_t wait_count = cmd_buffers[i].wait_semaphore_count();
        uint32_t signal_count = cmd_buffers[i].signal_semaphore_count();
#if OCARINA_VULKAN_FRAME_SYNC_TIMELINE
        if (signal_frame_timeline) {
            ++signal_count;
        }
#endif

        for (uint32_t j = 0; j < wait_count; ++j) {
            Semaphore* semaphore = cmd_buffers[i].get_wait_semaphore(j);
            waits[j].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waits[j].semaphore = reinterpret_cast<VkSemaphore>(semaphore->semaphore);
            // Swapchain acquire semaphores are binary; timeline value must be 0.
            waits[j].value = semaphore->timeline_value;
            waits[j].stageMask = vulkan_cmd_buffer->pipeline_stage_flags();
        }

        const uint32_t cmd_signal_count = cmd_buffers[i].signal_semaphore_count();
        for (uint32_t j = 0; j < cmd_signal_count; ++j) {
            Semaphore* semaphore = cmd_buffers[i].get_signal_semaphore(j);
            signals[j].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signals[j].semaphore = reinterpret_cast<VkSemaphore>(semaphore->semaphore);
            signals[j].value = semaphore->timeline_value;
            signals[j].stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }

#if OCARINA_VULKAN_FRAME_SYNC_TIMELINE
        if (signal_frame_timeline) {
            signals[cmd_signal_count].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signals[cmd_signal_count].semaphore = frame_timeline_sync_.timeline_semaphore;
            signals[cmd_signal_count].value = frame_timeline_sync_.frame_number;
            signals[cmd_signal_count].stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }
#endif

        VkSubmitInfo2 submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit.pNext = nullptr;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmd_buffers_submit[i];
        submit.waitSemaphoreInfoCount = wait_count;
        submit.pWaitSemaphoreInfos = wait_count > 0 ? waits.data() : nullptr;
        submit.signalSemaphoreInfoCount = signal_count;
        submit.pSignalSemaphoreInfos = signal_count > 0 ? signals.data() : nullptr;

        VkFence submit_fence = VK_NULL_HANDLE;
#if OCARINA_VULKAN_FRAME_SYNC_TIMELINE
        if (fence != VK_NULL_HANDLE && is_last_submit) {
            submit_fence = fence;
        }
#else
        if (fence != VK_NULL_HANDLE) {
            submit_fence = is_last_submit ? fence : VK_NULL_HANDLE;
        } else {
            submit_fence = is_last_submit ? frame_fence : VK_NULL_HANDLE;
        }
#endif

        queue_submit(graphics_queue, &submit, 1, submit_fence);
    }
}

Semaphore VulkanDriver::request_semaphore(VkSemaphoreType type, uint64_t timeline_value)
{
    Semaphore semaphore{};
    if (type == VK_SEMAPHORE_TYPE_BINARY) {
        VkSemaphore vk_semaphore;
        if (binary_semaphore_pool_.empty()) {
            VkSemaphoreCreateInfo semaphore_info{};
            semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            vkCreateSemaphore(device(), &semaphore_info, nullptr, &vk_semaphore);
        }
        else
        {
            vk_semaphore = binary_semaphore_pool_.front();
            binary_semaphore_pool_.erase(binary_semaphore_pool_.begin());
        }
        semaphore.semaphore = reinterpret_cast<handle_ty>(vk_semaphore);
        semaphore.timeline_value = 0;
        return semaphore;
    }
    else if (type == VK_SEMAPHORE_TYPE_TIMELINE) {
        
        VkSemaphore vk_semaphore;
        semaphore.is_timeline = true;
        if (timeline_semaphore_pool_.empty()) {
            VkSemaphoreTypeCreateInfo timeline_info{};
            timeline_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
            timeline_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            timeline_info.initialValue = 0;

            VkSemaphoreCreateInfo sem_info{};
            sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            sem_info.pNext = &timeline_info;

            vkCreateSemaphore(device(), &sem_info, nullptr, &vk_semaphore);
        }
        else
        {
            vk_semaphore = timeline_semaphore_pool_.front();
            timeline_semaphore_pool_.erase(timeline_semaphore_pool_.begin());
        }
        semaphore.semaphore = reinterpret_cast<handle_ty>(vk_semaphore);
    }
    return semaphore;
}

void VulkanDriver::recycle_semaphore(const Semaphore& semaphore)
{
    if (semaphore.is_timeline) {
        timeline_semaphore_pool_.push_back(reinterpret_cast<VkSemaphore>(semaphore.semaphore));
    }
    else {
        binary_semaphore_pool_.push_back(reinterpret_cast<VkSemaphore>(semaphore.semaphore));
    }
}

}// namespace ocarina



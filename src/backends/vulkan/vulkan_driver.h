#pragma once
#include "core/header.h"
#include "core/concepts.h"
#include "core/stl.h"
#include "rhi/graphics_descriptions.h"
#include <vulkan/vulkan.h>
#include "vulkan_pipeline.h"
#include <array>

namespace ocarina {
class VulkanPipelineManager;
class VulkanPipeline;
class RHIContext;
class VulkanDevice;
class VulkanShaderManager;
class VulkanDescriptorManager;
class VulkanShader;
struct InstanceCreation;
struct PipelineState;
class VulkanRenderPass;
class VulkanVertexBuffer;
class VulkanIndexBuffer;
struct VulkanVertexStreamBinding;
class VulkanDescriptorSetLayout;
class DescriptorSetLayout;
class VulkanDescriptorSet;
class VulkanTexture;
class TextureSampler;
class VulkanCommandBuffer;

class VulkanDriver : public concepts::Noncopyable {
private:
    enum internal_textures {
        INTERNAL_TEXTURE_WHITE,
        INTERNAL_TEXTURE_BLACK,
        INTERNAL_TEXTURE_NORMALMAP,
        INTERNAL_TEXTURE_COUNT
    };

public:
    ~VulkanDriver();
    static VulkanDriver& instance()
    {
        static VulkanDriver s_instance;
        return s_instance;
    }
    VulkanDevice *create_device(RHIContext *file_manager, const InstanceCreation &instance_creation);
    VulkanDevice *get_device() const { return vulkan_device_; }
    void bind_pipeline(VkCommandBuffer cmd_buffer, const VulkanPipeline &pipeline);
    void terminate();
    void submit_command_buffer(VkCommandBuffer cmd_buffer);
    inline VkDevice device() const;
    VulkanShader *create_shader(ShaderType shader_type,
                                const std::string &filename,
                                const std::set<std::string> &options,
                                const std::string &entry_point);
    VulkanShader* get_shader(handle_ty shader) const;
    OC_MAKE_MEMBER_GETTER(current_buffer, )
    [[nodiscard]] uint32_t current_frame() const noexcept { return current_frame_; }
    [[nodiscard]] uint32_t frames_in_flight() const noexcept { return frames_in_flight_; }
    //VkCommandBuffer get_current_command_buffer() const
    //{
    //    return draw_cmd_buffers_[current_buffer_];
    //}

    VkCommandBuffer begin_one_time_command_buffer();
    void end_one_time_command_buffer(VkCommandBuffer cmd);
    VkCommandBuffer begin_one_time_command_buffer(QueueType queue_type);
    void end_one_time_command_buffer(VkCommandBuffer cmd, QueueType queue_type);

    VulkanPipeline* get_pipeline(const PipelineState &pipeline_state, VkRenderPass render_pass);

    void begin_frame();
    void end_frame();

    // GPU frame timing (timestamps). Returns last resolved frame GPU time in ms.
    [[nodiscard]] double gpu_frame_time_ms() const noexcept { return gpu_frame_time_ms_; }
    void write_gpu_timestamp_begin(VkCommandBuffer cmd) noexcept;
    void write_gpu_timestamp_end(VkCommandBuffer cmd) noexcept;

    std::array<DescriptorSetLayout *, MAX_DESCRIPTOR_SETS_PER_SHADER> create_descriptor_set_layout(VulkanShader *shaders[], uint32_t shaders_count);
    //VkPipelineLayout get_pipeline_layout(VkDescriptorSetLayout *descriptset_layouts, uint8_t descriptset_layouts_count, VkPushConstantRange* push_constants, uint32_t push_constant_array_size);

    VulkanRenderPass* create_render_pass(const RenderPassCreation& render_pass_creation);
    void destroy_render_pass(VulkanRenderPass* render_pass);

    VkFramebuffer get_frame_buffer(uint32_t index)
    {
        return frame_buffers[index];
    }

    VkResult copy_buffer(VulkanBuffer* src, VulkanBuffer* dst);
    VkResult copy_buffer(VulkanBuffer *src, VkBuffer dst);

    void draw_triangles(VkCommandBuffer cmd, VulkanIndexBuffer* index_buffer);

    void push_constants(VkCommandBuffer cmd, VkPipelineLayout pipeline, const void *data, uint32_t size, uint32_t offset, VkShaderStageFlags stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    void bind_descriptor_sets(VkCommandBuffer cmd, DescriptorSet **descriptor_sets, uint32_t first_set, uint32_t descriptor_sets_num, VkPipelineLayout pipeline_layout);

    VkRenderPass get_framebuffer_render_pass() const {
        return renderpass_framebuffer;
    }

    void flush_command_buffer(VkCommandBuffer cmd);

    VulkanTexture* get_internal_white_texture()
    {
        return internal_textures_[INTERNAL_TEXTURE_WHITE];
    }

    VkSampler get_vulkan_sampler(const TextureSampler& sampler);
    /// Bindless sampler table: 0=linear wrap, 1=linear clamp, 2=point wrap, 3=point clamp.
    VkSampler get_bindless_sampler(uint32_t index);
    
    VkQueue get_graphics_queue() const {
        return graphics_queue;
    }

    VkQueue get_compute_queue() const {
        return compute_queue;
    }

    VkQueue get_copy_queue() const {
        return copy_queue;
    }

    VkDescriptorPool get_imgui_descriptor_pool();

    VulkanCommandBuffer* get_command_buffer(QueueType queue_type = QueueType::Graphics);
    void release_command_buffer(VulkanCommandBuffer* cmd_buffer);

    void queue_submit(VkQueue queue, const VkSubmitInfo2* submit_info, uint32_t submit_count, VkFence fence = VK_NULL_HANDLE);

    void execute_command_buffers(CommandBuffer* cmd_buffer, uint32_t counts, VkFence fence = VK_NULL_HANDLE);

    Semaphore request_semaphore(VkSemaphoreType type, uint64_t timeline_value = 0);

    void recycle_semaphore(const Semaphore& semaphore);

    VkSemaphore get_present_complete_semaphore() const {
        return frame_sync_[current_frame_].image_available;
    }

    VkSemaphore get_render_complete_semaphore() const {
        return frame_sync_[current_frame_].render_finished;
    }

    VkQueue get_queue(QueueType queue_type) const {
        if (queue_type == QueueType::Graphics) {
            return graphics_queue;
        } else if (queue_type == QueueType::Compute) {
            return compute_queue;
        } else if (queue_type == QueueType::Copy) {
            return copy_queue;
        }
        return VK_NULL_HANDLE;
    }
private:
    void setup_frame_buffer();
    //void setup_depth_stencil(uint32_t width, uint32_t height);
    void release_frame_buffer();
    void create_command_pool();
    void release_command_pool();
    void create_command_buffers();
    void release_command_buffers();
    void initialize();
    void window_resize();
    void create_internal_textures();
    void destroy_internal_textures();
    void create_frame_sync();
    void destroy_frame_sync();
private:
    static constexpr uint32_t kMaxFramesInFlight = 3;

    struct FrameSync {
        VkSemaphore image_available = VK_NULL_HANDLE;
        VkSemaphore render_finished = VK_NULL_HANDLE;
        VkFence in_flight_fence = VK_NULL_HANDLE;
    };
    VulkanDriver();
    VulkanDevice *vulkan_device_;
    std::unique_ptr<VulkanPipelineManager> vulkan_pipeline_manager;
    std::unique_ptr<VulkanShaderManager> vulkan_shader_manager;
    std::unique_ptr<VulkanDescriptorManager> vulkan_descriptor_manager;

    VkQueue graphics_queue{VK_NULL_HANDLE};
    VkQueue compute_queue{VK_NULL_HANDLE};
    VkQueue copy_queue{ VK_NULL_HANDLE };

    /** @brief Command pools per queue family */
    std::array<VkCommandPool, (size_t)QueueType::NumQueueType> command_pools_ = {};
    // Command buffers used for rendering
    //std::vector<VkCommandBuffer> draw_cmd_buffers_;

    using CommandBufferPoolPerQueue = std::array<std::queue<VulkanCommandBuffer*>, (size_t)QueueType::NumQueueType>;
    std::vector<CommandBufferPoolPerQueue> command_buffer_pools_;
    // Swapchain image index from the latest acquire
    uint32_t current_buffer_ = 0;
    // Ring index for per-frame CPU/GPU sync (fences, semaphores, command-buffer pools)
    uint32_t current_frame_ = 0;
    uint32_t frames_in_flight_ = 0;
    std::vector<FrameSync> frame_sync_;

    // Timestamp queries: 2 per frame (begin/end).
    VkQueryPool gpu_timestamp_query_pool_ = VK_NULL_HANDLE;
    double gpu_timestamp_period_ns_ = 0.0;
    double gpu_frame_time_ms_ = 0.0;
    // Tracks whether a frame-slot has valid timestamp data to read.
    std::vector<uint8_t> gpu_timestamp_written_;

    VkRenderPass renderpass_framebuffer{VK_NULL_HANDLE};
    //VkFormat depth_stencil_format;
    std::vector<VkFramebuffer> frame_buffers;

    std::vector<VulkanRenderPass *> render_passes_;
    std::unordered_map<uint64_t, VkSampler> samplers_;

    VulkanTexture *internal_textures_[INTERNAL_TEXTURE_COUNT] = {nullptr};
    VkDescriptorPool imgui_descriptorpool_ = VK_NULL_HANDLE;

    std::list<VkSemaphore> timeline_semaphore_pool_;
    std::list<VkSemaphore> binary_semaphore_pool_;
};
}// namespace ocarina
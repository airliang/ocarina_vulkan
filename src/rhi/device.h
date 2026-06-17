//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/image_base.h"
#include "core/concepts.h"
#include "core/thread_pool.h"
#include "graphics_descriptions.h"
#include "pipeline_state.h"
#include "command_buffer.h"
#include "fence.h"

namespace ocarina {

class RHIContext;

template<typename T>
class Buffer;

class Texture;

class VertexBuffer;
class IndexBuffer;
class RHIRenderPass;
class DescriptorSet;
class DescriptorSetLayout;
struct RHIPipeline;
class Image;
class TextureSampler;
struct ImguiCreation;

class OC_RHI_API Device : public concepts::Noncopyable {
public:
    class Impl : public concepts::Noncopyable {
    protected:
        RHIContext *context_{};
        friend class Device;

    public:
        explicit Impl(RHIContext *ctx) : context_(ctx) {}
        explicit Impl(RHIContext *ctx, const InstanceCreation &instance_creation) : context_(ctx) {}
        [[nodiscard]] virtual handle_ty create_buffer(size_t size, const string &desc, bool exported = false) noexcept = 0;
        virtual void destroy_buffer(handle_ty handle) noexcept = 0;
        [[nodiscard]] virtual handle_ty create_texture(uint3 res, PixelStorage pixel_storage,
                                                       uint level_num, const string &desc) noexcept = 0;
        [[nodiscard]] virtual handle_ty create_texture(Image *image, const TextureViewCreation &texture_view, const TextureSampler& sampler) noexcept = 0;
        [[nodiscard]] virtual handle_ty create_texture(uint32_t width, uint32_t height, uint32_t depth, PixelStorage pixel_storage,
                                                       const TextureViewCreation &texture_view, const TextureSampler& sampler,
                                                       uint4 default_color, const void *data) noexcept = 0;
        virtual void destroy_texture(handle_ty handle) noexcept = 0;
        [[nodiscard]] virtual handle_ty create_shader_from_file(const std::string &file_name, ShaderType shader_type, const std::set<string> &options) noexcept = 0;
        virtual void destroy_shader(handle_ty handle) noexcept = 0;
        [[nodiscard]] RHIContext *context() noexcept { return context_; }
        virtual VertexBuffer *create_vertex_buffer() noexcept = 0;
        virtual IndexBuffer *create_index_buffer(const void *initial_data, uint32_t indices_count, bool bit16) noexcept = 0;
        virtual void begin_frame() noexcept = 0;
        virtual void end_frame() noexcept = 0;
        virtual void wait_idle() noexcept {}
        virtual RHIRenderPass *create_render_pass(const RenderPassCreation &render_pass_creation) noexcept = 0;
        virtual void destroy_render_pass(RHIRenderPass *render_pass) noexcept = 0;
        virtual std::array<DescriptorSetLayout *, MAX_DESCRIPTOR_SETS_PER_SHADER> create_descriptor_set_layout(void **shaders, uint32_t shaders_count) noexcept = 0;
        virtual void bind_pipeline(const CommandBuffer& cmd_buffer, const handle_ty pipeline) noexcept = 0;
        virtual RHIPipeline *get_pipeline(const PipelineState &pipeline_state, RHIRenderPass *render_pass) noexcept = 0;

        virtual void memory_allocate(handle_ty *handle, size_t size, bool exported = true) {}
        virtual void memory_free(handle_ty *handle) {}

        virtual uint64_t get_aligned_memory_size(handle_ty handle) const { return 0; }

#if _WIN32 || _WIN64
        virtual handle_ty import_handle(handle_ty handle, size_t size) { return 0; }
        virtual uint64_t export_handle(handle_ty handle_) { return 0; }
#endif
        virtual void get_imgui_creation(ImguiCreation& imgui_creation) noexcept { }
        virtual CommandBuffer get_command_buffer() = 0;
        virtual void release_command_buffer(const CommandBuffer& cmd_buffer) = 0;
        virtual void execute_command_buffers(CommandBuffer* cmd_buffer, uint32_t count) noexcept {}
        virtual Semaphore get_present_complete_semaphore() noexcept = 0;
        virtual Semaphore get_render_complete_semaphore() noexcept = 0;
        virtual Fence create_fence() noexcept = 0;
        // Returns last completed frame GPU time in milliseconds (0 if unsupported).
        [[nodiscard]] virtual double gpu_frame_time_ms() const noexcept { return 0.0; }
    };

    using Creator = Device::Impl *(RHIContext *);
    using Deleter = void(Device::Impl *);
    using Handle = ocarina::unique_ptr<Device::Impl, Device::Deleter *>;

private:
    Handle impl_;

public:
    explicit Device(Handle impl) : impl_(std::move(impl)) {}
    [[nodiscard]] RHIContext *context() const noexcept { return impl_->context_; }
    template<typename T, typename... Args>
    [[nodiscard]] auto create(Args &&...args) const noexcept {
        return T(this->impl_.get(), std::forward<Args>(args)...);
    }
    template<typename T = std::byte>
    [[nodiscard]] Buffer<T> create_buffer(size_t size, const string &name = "") const noexcept {
        return Buffer<T>(impl_.get(), size, name);
    }

    void destroy_buffer(handle_ty handle) noexcept {
        impl_->destroy_buffer(handle);
    }

    [[nodiscard]] handle_ty import_handle(handle_ty handle, size_t size) noexcept {
        return impl_->import_handle(handle, size);
    }

    [[nodiscard]] uint64_t export_handle(handle_ty handle) noexcept {
        return impl_->export_handle(handle);
    }

    [[nodiscard]] uint64_t get_aligned_memory_size(handle_ty handle) const noexcept {
        return impl_->get_aligned_memory_size(handle);
    }

    [[nodiscard]] Texture create_texture(uint3 res, PixelStorage storage, const string &desc = "") const noexcept;
    [[nodiscard]] Texture create_texture(uint2 res, PixelStorage storage, const string &desc = "") const noexcept;
    [[nodiscard]] Texture create_texture(Image *image_resource, const TextureViewCreation &texture_view, const TextureSampler& sampler) const noexcept;
    [[nodiscard]] Texture create_texture(uint32_t width, uint32_t height, uint32_t depth, PixelStorage pixel_storage,
                                         const TextureViewCreation &texture_view, const TextureSampler& sampler,
                                         uint4 default_color = uint4(0, 0, 0, 255), const void *data = nullptr) const noexcept;

    [[nodiscard]] handle_ty create_shader_from_file(const std::string &file_name, ShaderType shader_type, std::set<std::string> &options) {
        return impl_->create_shader_from_file(file_name, shader_type, options);
    }

    [[nodiscard]] VertexBuffer *create_vertex_buffer() {
        return impl_->create_vertex_buffer();
    }

    [[nodiscard]] IndexBuffer *create_index_buffer(const void *initial_data, uint32_t indices_count, bool bit16 = true) {
        return impl_->create_index_buffer(initial_data, indices_count, bit16);
    }

    void begin_frame() {
        impl_->begin_frame();
    }

    void end_frame() {
        impl_->end_frame();
    }

    void wait_idle() noexcept {
        impl_->wait_idle();
    }

    [[nodiscard]] RHIRenderPass *create_render_pass(const RenderPassCreation &render_pass_creation) {
        return impl_->create_render_pass(render_pass_creation);
    }

    void destroy_render_pass(RHIRenderPass *render_pass) {
        impl_->destroy_render_pass(render_pass);
    }

    [[nodiscard]] std::array<DescriptorSetLayout *, MAX_DESCRIPTOR_SETS_PER_SHADER> create_descriptor_set_layout(void **shaders, uint32_t shaders_count) {
        return impl_->create_descriptor_set_layout(shaders, shaders_count);
    }

    void bind_pipeline(const CommandBuffer& cmd_buffer, const handle_ty pipeline) noexcept {
        impl_->bind_pipeline(cmd_buffer, pipeline);
    }

    RHIPipeline *get_pipeline(const PipelineState &pipeline_state, RHIRenderPass *render_pass) noexcept {
        return impl_->get_pipeline(pipeline_state, render_pass);
    }

    void get_imgui_creation(ImguiCreation& imgui_creation) {
        return impl_->get_imgui_creation(imgui_creation);
    }

    CommandBuffer get_command_buffer() noexcept {
        return impl_->get_command_buffer();
    }

    void release_command_buffer(const CommandBuffer& cmd_buffer) noexcept {
        impl_->release_command_buffer(cmd_buffer);
    }

    void execute_command_buffers(CommandBuffer* cmd_buffer, uint32_t count) noexcept {
        impl_->execute_command_buffers(cmd_buffer, count);
    }

    Semaphore get_present_complete_semaphore() noexcept {
        return impl_->get_present_complete_semaphore();
    }

    Semaphore get_render_complete_semaphore() noexcept {
        return impl_->get_render_complete_semaphore();
    }

    Fence create_fence() const noexcept {
        return impl_->create_fence();
    }

    [[nodiscard]] double gpu_frame_time_ms() const noexcept {
        return impl_->gpu_frame_time_ms();
    }

    Device::Impl* impl() noexcept { return impl_.get(); }

    [[nodiscard]] static Device create_device(const string &backend_name, const ocarina::InstanceCreation &instance_creation);
    [[nodiscard]] static Device create_device(const string &backend_name);
};

}// namespace ocarina

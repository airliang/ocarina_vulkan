#include "gpu_resource_thread.h"
#include "pinned_task_ids.h"
#include "frame_resources.h"
#include "bindless_texture_registry.h"
#include "global_gpu_storage.h"
#include "resource_manager.h"
#include "mesh.h"
#include "enki_task_debug.h"
#include "rhi/device.h"
#include "rhi/resources/texture.h"
#include "rhi/resources/resource.h"
#include "core/logging.h"

namespace ocarina {

void TextureGPUResourceRequest::process() {
    if (device == nullptr) {
        OC_ERROR("TextureGPUResourceRequest missing device");
        return;
    }

    Texture* texture = nullptr;
    switch (kind) {
    case GPUResourceRequestType::TextureFromData: {
        const void* data = pixel_data.empty() ? nullptr : pixel_data.data();
        texture = ocarina::new_with_allocator<Texture>(
            device->impl(),
            width,
            height,
            depth,
            pixel_storage,
            texture_view,
            sampler,
            uint4(0, 0, 0, 255),
            data);
        break;
    }
    case GPUResourceRequestType::RenderTarget:
        texture = ocarina::new_with_allocator<Texture>(
            device->impl(),
            width,
            height,
            pixel_storage,
            usage);
        break;
    default:
        break;
    }

    if (out_texture != nullptr) {
        *out_texture = texture;
    }

    if (texture == nullptr) {
        return;
    }

    texture->set_gpu_resource_state(GPUResourceState::GPU_Ready);

    const bool bindless =
        kind == GPUResourceRequestType::TextureFromData ||
        (static_cast<uint32_t>(usage) & static_cast<uint32_t>(TextureUsageFlags::ShaderReadOnly)) != 0;

    if (bindless) {
        if (bindless_index == InvalidUI32) {
            bindless_index = BindlessTextureRegistry::instance().allocate_index(texture);
        } else {
            BindlessTextureRegistry::instance().bind_texture(bindless_index, texture);
        }
        FrameResources::instance().queue_bindless_texture_update(bindless_index, texture);
    }

    if (has_cache_key) {
        ResourceManager::instance().complete_texture(cache_key, texture);
    }
}

void MeshGPUResourceRequest::process() {
    if (mesh == nullptr) {
        OC_ERROR("MeshGPUResourceRequest missing mesh");
        return;
    }

    MeshGeometryInput input{};
    input.vertex_count = static_cast<uint32_t>(positions.size());
    input.positions = positions.empty() ? nullptr : positions.data();
    input.normals = normals.empty() ? nullptr : normals.data();
    input.uvs = uvs.empty() ? nullptr : uvs.data();
    input.colors = colors.empty() ? nullptr : colors.data();
    input.indices = indices.empty() ? nullptr : indices.data();
    input.index_count = static_cast<uint32_t>(indices.size());

    const MeshGeometrySlice slice = GlobalGPUStorage::instance().upload_geometry(input);
    mesh->set_geometry_slice(slice);
    mesh->set_gpu_resource_state(GPUResourceState::GPU_Ready);
}

GPUResourceThread& GPUResourceThread::instance() {
    static GPUResourceThread s_instance;
    return s_instance;
}

GPUResourceThread::GPUResourceThread() noexcept
    : enki::IPinnedTask(pinned_task_thread_num(PinnedTaskIds::GPUResourceTask)) {}

void GPUResourceThread::start(enki::TaskScheduler& scheduler, Device* device) {
    device_ = device;
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    scheduler_ = &scheduler;
    shutdown_requested_.store(false, std::memory_order_release);
    scheduler.AddPinnedTask(this);
}

void GPUResourceThread::shutdown() {
    if (device_ != nullptr) {
        device_->wait_idle();
    }
    device_ = nullptr;
}

void GPUResourceThread::request_shutdown() {
    shutdown_requested_.store(true, std::memory_order_release);
    queue_.notify_all();
}

bool GPUResourceThread::should_stop() const noexcept {
    return shutdown_requested_.load(std::memory_order_acquire) ||
           (scheduler_ != nullptr && scheduler_->GetIsShutdownRequested());
}

void GPUResourceThread::Execute() {
    set_current_thread_name("GPU Resource Thread");

    while (true) {
        std::shared_ptr<GPUResourceRequest> request;
        if (!queue_.wait_and_pop(request, [this] { return should_stop(); })) {
            break;
        }

        do {
            if (request != nullptr) {
                request->process();
            }
        } while (queue_.try_pop(request));
    }

    running_.store(false, std::memory_order_release);
}

void GPUResourceThread::enqueue(std::shared_ptr<GPUResourceRequest> request) {
    if (request == nullptr) {
        return;
    }
    if (!is_running()) {
        request->process();
        return;
    }
    queue_.push(std::move(request));
}

}// namespace ocarina

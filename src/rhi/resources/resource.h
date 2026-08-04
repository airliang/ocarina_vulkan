//
// Created by Zero on 03/07/2022.
//

#pragma once

#include "core/stl.h"
#include "core/concepts.h"
#include "rhi/device.h"

namespace ocarina {

using handle_ty = uint64_t;

enum class GPUResourceState : uint8_t {
    CPU_Loaded = 0,///< Default: CPU-side data ready (or object constructed).
    GPU_Ready,     ///< Uploaded to GPU.
    GPU_Visible,   ///< Bound in a descriptor / bindless slot with a valid index.
};

class OC_RHI_API RHIResource : public concepts::Noncopyable {
public:
    enum Tag : uint8_t {
        BUFFER,
        TEXTURE,
        SHADER,
        MESH,
    };

protected:
    Tag tag_{};
    handle_ty handle_{};
    Device::Impl *device_{nullptr};
    GPUResourceState gpu_resource_state_{GPUResourceState::CPU_Loaded};

protected:
    RHIResource(Device::Impl *device, Tag tag, handle_ty handle)
        : device_(device), tag_(tag), handle_(handle) {}

    void _destroy();

public:
    RHIResource() = default;

    RHIResource(RHIResource &&other) noexcept {
        if (&other == this) { return; }
        tag_ = other.tag_;
        device_ = other.device_;
        handle_ = other.handle_;
        gpu_resource_state_ = other.gpu_resource_state_;
        other.device_ = nullptr;
        other.handle_ = 0;
        other.gpu_resource_state_ = GPUResourceState::CPU_Loaded;
    }

    RHIResource &operator=(RHIResource &&other) noexcept {
        if (&other == this) { return *this; }
        tag_ = other.tag_;
        device_ = other.device_;
        handle_ = other.handle_;
        gpu_resource_state_ = other.gpu_resource_state_;
        other.device_ = nullptr;
        other.handle_ = 0;
        other.gpu_resource_state_ = GPUResourceState::CPU_Loaded;
        return *this;
    }
    [[nodiscard]] Tag tag() const noexcept { return tag_; }
    [[nodiscard]] virtual handle_ty handle() const noexcept { return handle_; }
    virtual void set_device(Device::Impl *device) noexcept { device_ = device; }
    OC_MAKE_MEMBER_GETTER(device, )
    [[nodiscard]] GPUResourceState gpu_resource_state() const noexcept { return gpu_resource_state_; }
    void set_gpu_resource_state(GPUResourceState state) noexcept { gpu_resource_state_ = state; }
    [[nodiscard]] virtual const void *handle_ptr() const noexcept { return &handle_; }
    [[nodiscard]] virtual void *handle_ptr() noexcept { return &handle_; }
    [[nodiscard]] virtual size_t data_size() const noexcept { return sizeof(handle_ty); }
    [[nodiscard]] virtual size_t data_alignment() const noexcept { return sizeof(handle_ty); }
    [[nodiscard]] bool valid() const noexcept { return bool(device_); }
    virtual void destroy() { _destroy(); }
    virtual ~RHIResource() { _destroy(); }
};

class ExportableResource : public RHIResource {
protected:
    bool exported_{true};
    explicit ExportableResource(Device::Impl *device, Tag tag,
                                handle_ty handle, bool exported)
        : RHIResource(device, tag, handle), exported_(exported) {}

public:
    using RHIResource::RHIResource;

    struct Data {
        handle_ty handle;
        size_t size;
    };

#if _WIN32 || _WIN64
    virtual void import_handle(uint64_t handle) { OC_ASSERT(0); };
    virtual uint64_t export_handle() {
        OC_ASSERT(0);
        return 0;
    };
#endif
    OC_MAKE_MEMBER_GETTER(exported, )
};

}// namespace ocarina

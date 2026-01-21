//
// Created by Zero on 12/01/2023.
//

#pragma once

#include "core/stl.h"
#include "rhi/resources/managed.h"
#include "rhi/resources/bindless_array.h"
#include <cuda.h>

namespace ocarina {
class CUDADevice;
class CUDABindlessArray : public BindlessArray::Impl {

private:
    BindlessArrayDesc slot_soa_{};
    CUDADevice *device_{};
    Managed<ByteBufferDesc> buffers_;
    Managed<CUtexObject> textures_;

public:
    explicit CUDABindlessArray(CUDADevice *device);

    /// for device side structure
    [[nodiscard]] const void *handle_ptr() const noexcept override {
        return &slot_soa_;
    }
    [[nodiscard]] size_t max_member_size() const noexcept override { return sizeof(CUdeviceptr); }
    [[nodiscard]] size_t data_size() const noexcept override { return sizeof(BindlessArrayDesc); }
    [[nodiscard]] size_t data_alignment() const noexcept override { return alignof(BindlessArrayDesc); }
    void prepare_slotSOA(Device &device) noexcept override;
    [[nodiscard]] CommandList update_slotSOA(bool async) noexcept override;

    [[nodiscard]] size_t emplace_buffer(handle_ty handle, uint offset_in_byte,
                                        size_t size_in_byte) noexcept override;
    void remove_buffer(handle_ty index) noexcept override;
    [[nodiscard]] size_t emplace_texture(handle_ty handle) noexcept override;
    void remove_texture(handle_ty index) noexcept override;
    void set_buffer(ocarina::handle_ty index, ocarina::handle_ty handle, uint offset_in_byte,
                    size_t size_in_byte) noexcept override;
    [[nodiscard]] ByteBufferDesc buffer_view(ocarina::uint index) const noexcept override;
    void set_texture(ocarina::handle_ty index, ocarina::handle_ty handle) noexcept override;
    [[nodiscard]] size_t buffer_num() const noexcept override;
    [[nodiscard]] size_t texture_num() const noexcept override;
    [[nodiscard]] size_t buffer_slots_size() const noexcept override;
    [[nodiscard]] size_t tex_slots_size() const noexcept override;
    [[nodiscard]] BufferUploadCommand *upload_buffer_handles(bool async) const noexcept override;
    [[nodiscard]] BufferUploadCommand *upload_texture_handles(bool async) const noexcept override;
};

}// namespace ocarina
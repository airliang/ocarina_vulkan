//
// Created by Zero on 12/01/2023.
//

#include "cuda_bindless_array.h"
#include "cuda_device.h"

namespace ocarina {

CUDABindlessArray::CUDABindlessArray(CUDADevice *device)
    : device_(device), buffers_(device, c_max_slot_num, "CUDABindlessArray::buffers_"),
      textures_(device, c_max_slot_num, "CUDABindlessArray::textures_") {
    slot_soa_.buffer_slot = buffers_.handle();
    slot_soa_.tex_slot = textures_.handle();
}

size_t CUDABindlessArray::emplace_buffer(handle_ty handle, uint offset_in_byte, size_t size_in_byte) noexcept {
    auto ret = buffers_.host_buffer().size();
    buffers_.emplace_back(reinterpret_cast<std::byte *>(handle), offset_in_byte, size_in_byte);
    OC_ERROR_IF(ret >= c_max_slot_num, ocarina::format("slot_size is {}, buffer_num is {}", c_max_slot_num, ret));
    return ret;
}

size_t CUDABindlessArray::emplace_texture(handle_ty handle) noexcept {
    auto ret = textures_.host_buffer().size();
    textures_.push_back(handle);
    OC_ERROR_IF(ret >= c_max_slot_num, ocarina::format("slot_size is {}, tex_num is {}", c_max_slot_num, ret));
    return ret;
}

void CUDABindlessArray::prepare_slotSOA(Device &device) noexcept {
    buffers_.reset_device_buffer_immediately(device);
    textures_.reset_device_buffer_immediately(device);
    slot_soa_.buffer_slot = buffers_.handle();
    slot_soa_.tex_slot = textures_.handle();
}

CommandList CUDABindlessArray::update_slotSOA(bool async) noexcept {
    CommandList ret;
    append(ret, buffers_.device_buffer().reallocate(buffers_.host_buffer().size(), async));
    append(ret, textures_.device_buffer().reallocate(textures_.host_buffer().size(), async));
    ret.push_back(HostFunctionCommand::create([&]() {
        slot_soa_.buffer_slot = buffers_.handle();
        slot_soa_.tex_slot = textures_.handle();
    },
                                              async));
    return ret;
}

namespace detail {

template<typename T>
void remove_by_index(vector<T> &v, handle_ty index) noexcept {
    auto iter = v.begin();
    for (int i = 0; i < v.size(); ++i, ++iter) {
        if (i == index) {
            v.erase(iter);
            return;
        }
    }
}

}// namespace detail

void CUDABindlessArray::remove_buffer(handle_ty index) noexcept {
    detail::remove_by_index(buffers_.host_buffer(), index);
}

void CUDABindlessArray::remove_texture(handle_ty index) noexcept {
    detail::remove_by_index(textures_.host_buffer(), index);
}

void CUDABindlessArray::set_buffer(ocarina::handle_ty index, ocarina::handle_ty handle,
                                   uint offset_in_byte, size_t size_in_byte) noexcept {
    OC_ASSERT(index < buffers_.host_buffer().size());
    buffers_.host_buffer().at(index) = {reinterpret_cast<std::byte *>(handle),
                                        offset_in_byte, size_in_byte};
}

ByteBufferDesc CUDABindlessArray::buffer_view(ocarina::uint index) const noexcept {
    return buffers_.host_buffer().at(index);
}

void CUDABindlessArray::set_texture(ocarina::handle_ty index, ocarina::handle_ty handle) noexcept {
    OC_ASSERT(index < textures_.host_buffer().size());
    textures_.host_buffer().at(index) = handle;
}

size_t CUDABindlessArray::buffer_num() const noexcept {
    return buffers_.host_buffer().size();
}

size_t CUDABindlessArray::texture_num() const noexcept {
    return textures_.host_buffer().size();
}

size_t CUDABindlessArray::buffer_slots_size() const noexcept {
    return sizeof(ByteBufferDesc) * c_max_slot_num;
}

size_t CUDABindlessArray::tex_slots_size() const noexcept {
    return sizeof(CUtexObject) * c_max_slot_num;
}

BufferUploadCommand *CUDABindlessArray::upload_texture_handles(bool async) const noexcept {
    return textures_.upload(async);
}

BufferUploadCommand *CUDABindlessArray::upload_buffer_handles(bool async) const noexcept {
    return buffers_.upload(async);
}

}// namespace ocarina
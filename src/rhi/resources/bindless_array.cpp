//
// Created by Zero on 12/01/2023.
//

#include "bindless_array.h"
#include "texture.h"
#include "buffer.h"
#include "byte_buffer.h"
#include "managed.h"

namespace ocarina {

BindlessArray::BindlessArray(Device::Impl *device)
    : RHIResource(device, Tag::BINDLESS_ARRAY,
                  device->create_bindless_array()) {}

size_t BindlessArray::emplace(const Texture &texture) noexcept {
    return impl()->emplace_texture(texture.tex_handle());
}

void BindlessArray::set_texture(ocarina::handle_ty index,
                                const ocarina::Texture &texture) noexcept {
    impl()->set_texture(index, texture.tex_handle());
}

ByteBufferView BindlessArray::byte_buffer_view(ocarina::uint index) const noexcept {
    ByteBufferDesc buffer_desc = impl()->buffer_view(index);
    return {buffer_desc.head(), buffer_desc.size_in_byte()};
}

CommandList BindlessArray::upload_handles(bool async) noexcept {
    CommandList ret;
    ret.push_back(impl()->upload_buffer_handles(async));
    ret.push_back(impl()->upload_texture_handles(async));
    return ret;
}

uint BindlessArray::buffer_num() const noexcept {
    return impl()->buffer_num();
}

uint BindlessArray::texture_num() const noexcept {
    return impl()->texture_num();
}

}// namespace ocarina
//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "resource.h"
#include "rhi/graphics_descriptions.h"
#include <type_traits>

namespace ocarina {

template<typename T = std::byte>
struct BufferDesc {
    T *handle{};
    uint offset{};
    uint64_t size{};

    [[nodiscard]] handle_ty head() const noexcept {
        return reinterpret_cast<handle_ty>(handle);
    }

    [[nodiscard]] uint64_t size_in_byte() const noexcept {
        return size * sizeof(T);
    }

    [[nodiscard]] uint offset_in_byte() const noexcept {
        return offset * sizeof(T);
    }
};

namespace detail {
template<typename T>
struct is_valid_buffer_element_impl : std::bool_constant<std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>> {};
}// namespace detail

template<typename T>
constexpr bool is_valid_buffer_element_v = detail::is_valid_buffer_element_impl<T>::value;

/// Non-template GPU buffer resource. Concrete backends (e.g. VulkanBuffer) implement map/unmap.
/// Owned by ResourceManager and looked up by handle_ty (backend Buffer* cast to uint64_t).
class Buffer : public ExportableResource {
public:
    using Super = ExportableResource;

    Buffer() = default;
    ~Buffer() override = default;

    Buffer(Buffer &&other) noexcept
        : Super(std::move(other)) {
        size_in_byte_ = other.size_in_byte_;
        mapped_ = other.mapped_;
        other.size_in_byte_ = 0;
        other.mapped_ = nullptr;
    }

    Buffer &operator=(Buffer &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        destroy();
        Super::operator=(std::move(other));
        size_in_byte_ = other.size_in_byte_;
        mapped_ = other.mapped_;
        other.size_in_byte_ = 0;
        other.mapped_ = nullptr;
        return *this;
    }

    void destroy() override {
        _destroy();
        size_in_byte_ = 0;
        mapped_ = nullptr;
    }

    [[nodiscard]] size_t size_in_byte() const noexcept { return size_in_byte_; }
    void set_size_in_byte(size_t size) noexcept { size_in_byte_ = size; }

    void copy_from_immediately(const void *src, uint32_t size, uint32_t dst_offset = 0) noexcept {
        if (src == nullptr || size == 0) {
            return;
        }
        if (mapped_ == nullptr) {
            map();
        }
        if (mapped_ == nullptr) {
            return;
        }
        memcpy(static_cast<std::byte *>(mapped_) + dst_offset, src, size);
    }

protected:
    Buffer(Device::Impl *device, handle_ty handle, size_t size_in_byte, bool exported = false)
        : Super(device, Tag::BUFFER, handle, exported),
          size_in_byte_(size_in_byte) {}

    virtual void map() noexcept = 0;
    virtual void unmap() noexcept = 0;

    size_t size_in_byte_ = 0;
    void *mapped_ = nullptr;
};

template<typename T>
class TypedBuffer;

template<typename T>
class BufferRegion {
private:
    handle_ty handle_{};
    size_t offset_{};
    size_t size_{};
    size_t total_size_{};

public:
    BufferRegion() = default;
    BufferRegion(handle_ty handle, size_t offset, size_t size, size_t total_size)
        : handle_(handle), offset_(offset), size_(size), total_size_(total_size) {}
    BufferRegion(const TypedBuffer<T> &buffer, size_t offset = 0, size_t size = 0);

    [[nodiscard]] handle_ty handle() const { return handle_; }
    [[nodiscard]] size_t size() const { return size_; }
    [[nodiscard]] static constexpr size_t element_size() noexcept { return sizeof(T); }
    [[nodiscard]] size_t size_in_byte() const noexcept { return size_ * element_size(); }
    [[nodiscard]] size_t offset() const noexcept { return offset_; }
    [[nodiscard]] size_t offset_in_byte() const noexcept { return offset_ * element_size(); }
    [[nodiscard]] size_t total_size_in_byte() const noexcept { return total_size_ * element_size(); }
    [[nodiscard]] size_t total_size() const noexcept { return total_size_; }
};

/// Lightweight typed view of a ResourceManager-owned Buffer.
/// `handle_` is the backend buffer object pointer cast to handle_ty (same as Device::Impl::create_buffer).
template<typename T = std::byte>
class TypedBuffer {
    static_assert(is_valid_buffer_element_v<T>);

public:
    using element_type = T;

    TypedBuffer() = default;
    TypedBuffer(handle_ty handle, size_t element_count) noexcept
        : handle_(handle), size_(element_count) {}

    [[nodiscard]] static constexpr size_t element_size() noexcept { return sizeof(T); }

    [[nodiscard]] handle_ty handle() const noexcept { return handle_; }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] size_t size_in_byte() const noexcept { return size_ * element_size(); }
    [[nodiscard]] bool valid() const noexcept { return handle_ != 0; }

    [[nodiscard]] Buffer *buffer() const noexcept {
        return handle_ != 0 ? reinterpret_cast<Buffer *>(handle_) : nullptr;
    }

    [[nodiscard]] BufferRegion<T> region(size_t offset = 0, size_t count = 0) const noexcept {
        count = count == 0 ? size_ - offset : count;
        return BufferRegion<T>(handle_, offset, count, size_);
    }

    void copy_from_immediately(const void *src, uint32_t byte_size, uint32_t dst_offset = 0) noexcept {
        if (Buffer *b = buffer()) {
            b->copy_from_immediately(src, byte_size, dst_offset);
        }
    }

    void reset() noexcept {
        handle_ = 0;
        size_ = 0;
    }

private:
    handle_ty handle_ = 0;
    size_t size_ = 0;
};

template<typename T>
BufferRegion<T>::BufferRegion(const TypedBuffer<T> &buffer, size_t offset, size_t size)
    : BufferRegion(
          buffer.handle(),
          offset,
          size == 0 ? buffer.size() - offset : size,
          buffer.size()) {}

}// namespace ocarina

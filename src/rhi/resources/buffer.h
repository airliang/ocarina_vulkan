//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "resource.h"
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

template<typename T>
class Buffer;

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
    BufferRegion(const Buffer<T> &buffer, size_t offset = 0, size_t size = 0);

    [[nodiscard]] handle_ty handle() const { return handle_; }
    [[nodiscard]] size_t size() const { return size_; }
    [[nodiscard]] static constexpr size_t element_size() noexcept { return sizeof(T); }
    [[nodiscard]] size_t size_in_byte() const noexcept { return size_ * element_size(); }
    [[nodiscard]] size_t offset() const noexcept { return offset_; }
    [[nodiscard]] size_t offset_in_byte() const noexcept { return offset_ * element_size(); }
    [[nodiscard]] size_t total_size_in_byte() const noexcept { return total_size_ * element_size(); }
    [[nodiscard]] size_t total_size() const noexcept { return total_size_; }
};

template<typename T = std::byte>
class Buffer : public ExportableResource {
    static_assert(is_valid_buffer_element_v<T>);

public:
    using element_type = T;
    using Super = ExportableResource;

protected:
    size_t size_{};
    mutable BufferDesc<T> descriptor_{};

public:
    Buffer() = default;

    [[nodiscard]] static constexpr size_t element_size() noexcept { return sizeof(T); }

    Buffer(Device::Impl *device, size_t size, const string &desc = "", bool exported = false)
        : Super(device, Tag::BUFFER, device->create_buffer(size * element_size(), desc, exported), exported),
          size_(size) {
        descriptor_.handle = reinterpret_cast<T *>(handle_);
        descriptor_.offset = 0u;
        descriptor_.size = size_;
    }

    void destroy() override {
        _destroy();
        size_ = 0;
    }

    [[nodiscard]] BufferRegion<T> region(size_t offset = 0, size_t size = 0) const noexcept {
        size = size == 0 ? size_ - offset : size;
        return BufferRegion<T>(handle_, offset, size, size_);
    }

    Buffer(Buffer &&other) noexcept
        : Super(std::move(other)) {
        this->size_ = other.size_;
        this->descriptor_ = other.descriptor_;
    }

    Buffer &operator=(Buffer &&other) noexcept {
        destroy();
        Super::operator=(std::move(other));
        this->size_ = other.size_;
        this->descriptor_ = other.descriptor_;
        return *this;
    }

    const BufferDesc<T> &descriptor() const noexcept {
        descriptor_.handle = reinterpret_cast<T *>(handle_);
        descriptor_.offset = 0u;
        descriptor_.size = size_;
        return descriptor_;
    }

    template<typename U>
    [[nodiscard]] auto ptr() const noexcept {
        if constexpr (std::is_same_v<U, handle_ty>) {
            return handle();
        } else {
            return reinterpret_cast<U>(handle());
        }
    }

    template<typename U>
    [[nodiscard]] auto ptr() noexcept {
        if constexpr (std::is_same_v<U, handle_ty>) {
            return handle();
        } else {
            return reinterpret_cast<U>(handle());
        }
    }

    void set_size(size_t size) noexcept { size_ = size; }

    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] size_t size_in_byte() const noexcept { return size_ * sizeof(T); }

    void copy_from_immediately(const void *src, uint32_t size, uint32_t dst_offset = 0) noexcept {
        if (src == nullptr || size == 0) return;
        if (mapped_ == nullptr) {
            map();
        }
        memcpy(static_cast<std::byte *>(mapped_) + dst_offset, src, size);
    }

protected:
    virtual void map() noexcept {}
    virtual void unmap() noexcept {}

    void *mapped_ = nullptr;
};

template<typename T>
BufferRegion<T>::BufferRegion(const Buffer<T> &buffer, size_t offset, size_t size)
    : BufferRegion(buffer.handle(), offset, size == 0 ? buffer.size() - offset : size, buffer.size()) {}

}// namespace ocarina

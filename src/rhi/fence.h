//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "graphics_descriptions.h"

namespace ocarina {

class Fence
{
public:
    class Impl : public concepts::Noncopyable {
    public:
        virtual ~Impl() {}
        virtual void reset() = 0;
        virtual bool is_finished() const = 0;
        virtual handle_ty native_handle() const = 0;
        virtual void wait(uint64_t timeout_ns = std::numeric_limits<uint64_t>::max()) const = 0;
    };

public:
    struct ImplDeleter {
        void operator()(Impl* ptr) const noexcept {
            if (ptr) {
                ocarina::delete_with_allocator(ptr);
            }
        }
    };
    using UniqueImplPtr = std::unique_ptr<Impl, ImplDeleter>;

    Fence() = default;
    explicit Fence(UniqueImplPtr impl) : impl_(std::move(impl)) {}
    Fence(Fence&&) noexcept = default;
    Fence& operator=(Fence&&) noexcept = default;
    Fence(const Fence&) = delete;
    Fence& operator=(const Fence&) = delete;
    ~Fence() = default;

    void reset() const
    {
        if (impl_) {
            impl_->reset();
        }
    }

    bool is_finished() const
    {
        if (impl_) {
            return impl_->is_finished();
        }
        return true;
    }

    handle_ty native_handle() const
    {
        return impl_ ? impl_->native_handle() : handle_ty{};
    }

    void wait(uint64_t timeout_ns = std::numeric_limits<uint64_t>::max()) const
    {
        if (impl_) {
            impl_->wait(timeout_ns);
        }
    }
private:
    UniqueImplPtr impl_;
};

}// namespace ocarina
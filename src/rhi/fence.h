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
    };

public:
    Fence(std::unique_ptr<Impl> impl)
    {
        impl_ = std::move(impl);
    }
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



    [[nodiscard]] static Fence create_fence();
private:
    std::unique_ptr<Impl> impl_;
};

}// namespace ocarina
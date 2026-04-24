//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "fence.h"
#include "context.h"

namespace ocarina {

    Fence Fence::create_fence()
    {
        auto d = RHIContext::instance().obtain_module(RHIContext::instance().current_backend());
        // The function_ptr likely returns a void* or a function pointer, so we need to cast it to the correct type.
        using CreateFenceFunc = Impl* (*)();
        auto create_fence_func = reinterpret_cast<CreateFenceFunc>(d->function_ptr("create_fence"));
        return Fence(std::unique_ptr<Impl>(create_fence_func()));
    }

}// namespace ocarina
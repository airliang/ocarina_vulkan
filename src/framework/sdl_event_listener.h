#pragma once

#include "core/header.h"
#include <SDL3/SDL_events.h>

namespace ocarina {

class OC_FRAMEWORK_API SDLWindowEventListener {
public:
    virtual ~SDLWindowEventListener() noexcept = default;

    virtual void process_sdl_event(const SDL_Event& event) noexcept = 0;
};

}// namespace ocarina

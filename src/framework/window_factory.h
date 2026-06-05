#pragma once

#include "core/header.h"
#include "window_decl.h"

namespace ocarina {

[[nodiscard]] OC_FRAMEWORK_API SDLWindowWrapper create_sdl_window(
    const char *name,
    uint2 initial_size,
    bool resizable = false);

}// namespace ocarina

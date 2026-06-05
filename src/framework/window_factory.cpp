#include "window_factory.h"
#include "sdl_window.h"

namespace ocarina {

namespace {

void destroy_sdl_window(SDLWindow *window) noexcept {
    delete_with_allocator(window);
}

}// namespace

SDLWindowWrapper create_sdl_window(const char *name, uint2 initial_size, bool resizable) {
    return SDLWindowWrapper(
        new_with_allocator<SDLWindow>(name, initial_size, resizable),
        destroy_sdl_window);
}

}// namespace ocarina

#pragma once

#include "core/stl.h"
#include "math/basic_types.h"

namespace ocarina {

class SDLWindow;

using SDLWindowDeleter = void(SDLWindow *);
using SDLWindowWrapper = ocarina::unique_ptr<SDLWindow, SDLWindowDeleter *>;

}// namespace ocarina

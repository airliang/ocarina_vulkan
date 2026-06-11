//
// Created by Zero on 2022/8/10.
//

#pragma once

#include "core/stl.h"

namespace ocarina {

enum ShaderTag : uint8_t {
    CS = 1 << 1,
    VS = 1 << 2,
    FS = 1 << 3,
    GS = 1 << 4,
    TS = 1 << 5
};

}// namespace ocarina

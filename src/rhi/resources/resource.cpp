//
// Created by Zero on 06/07/2022.
//

#include "resource.h"

namespace ocarina {

void RHIResource::_destroy() {
    if (!valid()) {
        return;
    }
    if (handle_ == 0) {
        return;
    }
    switch (tag_) {
        case Tag::BUFFER: device_->destroy_buffer(handle_); break;
        case Tag::TEXTURE: device_->destroy_texture(handle_); break;
        case Tag::SHADER: device_->destroy_shader(handle_); break;
    }
    handle_ = 0;
}
}// namespace ocarina

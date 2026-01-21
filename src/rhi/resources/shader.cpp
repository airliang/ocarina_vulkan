//
// Created by Zero on 09/07/2022.
//

#include "shader.h"
#include "texture.h"

namespace ocarina {

void ArgumentList::_encode_texture(const Texture &texture) noexcept {
    push_memory_block(texture.memory_block());
}

void ArgumentList::_encode_bindless_array(const BindlessArray &bindless_array) noexcept {
    push_memory_block(bindless_array.memory_block());
}

}// namespace ocarina
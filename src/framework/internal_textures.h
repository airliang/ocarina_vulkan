#pragma once

#include "core/header.h"
#include "core/concepts.h"

namespace ocarina {

class Device;
class Texture;

class OC_FRAMEWORK_API InternalTextures : public concepts::Noncopyable {
public:
    static InternalTextures& instance();

    Texture* get_white_texture(Device* device);
    void cleanup();

private:
    InternalTextures() = default;

    Texture* white_texture_ = nullptr;
    Device* device_ = nullptr;
};

}// namespace ocarina

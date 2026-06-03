#include "bindless_texture_registry.h"
#include "rhi/resources/texture.h"

namespace ocarina {

BindlessTextureRegistry& BindlessTextureRegistry::instance() {
    static BindlessTextureRegistry s_instance;
    return s_instance;
}

uint32_t BindlessTextureRegistry::allocate_index(Texture* texture) {
    if (texture == nullptr) {
        return InvalidUI32;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = texture_to_index_.find(texture);
    if (it != texture_to_index_.end()) {
        return it->second;
    }
    const uint32_t index = static_cast<uint32_t>(texture_order_.size());
    texture_order_.push_back(texture);
    texture_to_index_.insert(std::make_pair(texture, index));
    return index;
}

uint32_t BindlessTextureRegistry::get_index(Texture* texture) const {
    if (texture == nullptr) {
        return InvalidUI32;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = texture_to_index_.find(texture);
    return it != texture_to_index_.end() ? it->second : InvalidUI32;
}

size_t BindlessTextureRegistry::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return texture_order_.size();
}

}// namespace ocarina

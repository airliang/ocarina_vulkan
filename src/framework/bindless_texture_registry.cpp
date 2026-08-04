#include "bindless_texture_registry.h"
#include "rhi/resources/texture.h"

namespace ocarina {

BindlessTextureRegistry& BindlessTextureRegistry::instance() {
    static BindlessTextureRegistry s_instance;
    return s_instance;
}

uint32_t BindlessTextureRegistry::allocate_slot() {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint32_t index = static_cast<uint32_t>(texture_order_.size());
    texture_order_.push_back(nullptr);
    return index;
}

void BindlessTextureRegistry::bind_texture(uint32_t index, Texture* texture) {
    if (index == InvalidUI32 || texture == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= texture_order_.size()) {
        return;
    }
    Texture* previous = texture_order_[index];
    if (previous != nullptr && previous != texture) {
        texture_to_index_.erase(previous);
    }
    texture_order_[index] = texture;
    texture_to_index_[texture] = index;
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

Texture* BindlessTextureRegistry::get_texture(uint32_t index) const {
    if (index == InvalidUI32) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= texture_order_.size()) {
        return nullptr;
    }
    return texture_order_[index];
}

size_t BindlessTextureRegistry::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return texture_order_.size();
}

}// namespace ocarina

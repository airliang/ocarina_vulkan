#pragma once

#include "core/header.h"
#include "core/concepts.h"
#include "core/stl.h"
#include <mutex>

namespace ocarina {

class Texture;

/// Process-wide bindless array index allocator.
/// Indices can be reserved before the GPU Texture exists.
class OC_FRAMEWORK_API BindlessTextureRegistry : public concepts::Noncopyable {
public:
    static BindlessTextureRegistry& instance();

    /// Reserve a bindless slot immediately (Texture may be bound later).
    [[nodiscard]] uint32_t allocate_slot();

    /// Bind a GPU texture into a previously reserved slot.
    void bind_texture(uint32_t index, Texture* texture);

    /// Allocate (or reuse) a slot for an existing texture.
    uint32_t allocate_index(Texture* texture);

    [[nodiscard]] uint32_t get_index(Texture* texture) const;
    [[nodiscard]] Texture* get_texture(uint32_t index) const;
    [[nodiscard]] size_t count() const;

    template<typename Fn>
    void for_each(Fn&& fn) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < texture_order_.size(); ++i) {
            fn(static_cast<uint32_t>(i), texture_order_[i]);
        }
    }

private:
    BindlessTextureRegistry() = default;

    mutable std::mutex mutex_;
    std::vector<Texture*> texture_order_;
    std::unordered_map<Texture*, uint32_t> texture_to_index_;
};

}// namespace ocarina

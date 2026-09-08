#pragma once

#include "core/header.h"
#include "core/concepts.h"
#include "core/stl.h"

#include <limits>
#include <memory>

namespace ocarina {

class Device;

/// GPU queue synchronization primitive (binary or timeline).
/// Copies share the underlying native semaphore via shared Impl ownership.
/// `timeline_value` / `stage_mask` are per-submit parameters and are copied with the object.
class OC_RHI_API Semaphore {
public:
    class Impl : public concepts::Noncopyable {
    public:
        virtual ~Impl() = default;
        [[nodiscard]] virtual handle_ty native_handle() const noexcept = 0;
        [[nodiscard]] virtual bool is_timeline() const noexcept = 0;
        [[nodiscard]] virtual uint64_t get_counter_value() const noexcept = 0;
        virtual void wait(
            uint64_t value,
            uint64_t timeout_ns = std::numeric_limits<uint64_t>::max()) const = 0;
    };

public:
    Semaphore() = default;
    Semaphore(Device *device, std::shared_ptr<Impl> impl) noexcept
        : device_(device), impl_(std::move(impl)) {}
    Semaphore(const Semaphore &) = default;
    Semaphore &operator=(const Semaphore &) = default;
    Semaphore(Semaphore &&) noexcept = default;
    Semaphore &operator=(Semaphore &&) noexcept = default;
    ~Semaphore() = default;

    [[nodiscard]] Device *device() const noexcept { return device_; }
    [[nodiscard]] bool valid() const noexcept {
        return impl_ != nullptr && native_handle() != 0 && native_handle() != InvalidUI64;
    }

    [[nodiscard]] handle_ty native_handle() const noexcept {
        return impl_ ? impl_->native_handle() : handle_ty{};
    }

    [[nodiscard]] bool is_timeline() const noexcept {
        return impl_ ? impl_->is_timeline() : false;
    }

    [[nodiscard]] uint64_t get_counter_value() const noexcept {
        return impl_ ? impl_->get_counter_value() : 0;
    }

    /// Block the host until the timeline reaches at least `value`.
    void wait(
        uint64_t value,
        uint64_t timeout_ns = std::numeric_limits<uint64_t>::max()) const {
        if (impl_) {
            impl_->wait(value, timeout_ns);
        }
    }

    /// Convenience: wait for this object's current `timeline_value`.
    void wait_for_timeline_value(
        uint64_t timeout_ns = std::numeric_limits<uint64_t>::max()) const {
        wait(timeline_value, timeout_ns);
    }

    /// Timeline value to wait/signal with on queue submit.
    uint64_t timeline_value = 0;
    /// Optional VkPipelineStageFlags2 for wait; 0 means use the command buffer default.
    uint64_t stage_mask = 0;

private:
    Device *device_ = nullptr;
    std::shared_ptr<Impl> impl_;
};

}// namespace ocarina

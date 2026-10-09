#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/logging.h"

namespace ocarina {

/// Sparse set component storage: only entities that add T consume dense slots.
template<typename T>
class SparseComponentPool {
public:
    using dense_vector = ocarina_vector<T>;
    using index_vector = ocarina_vector<uint32_t>;

    SparseComponentPool(
        const char* dense_pool_name,
        const char* dense_to_entity_pool_name,
        const char* sparse_pool_name)
        : dense_(ocarina_pool_allocator<T>(dense_pool_name)),
          dense_to_entity_(ocarina_pool_allocator<uint32_t>(dense_to_entity_pool_name)),
          sparse_(ocarina_pool_allocator<uint32_t>(sparse_pool_name)) {}

    SparseComponentPool(const SparseComponentPool&) = delete;
    SparseComponentPool& operator=(const SparseComponentPool&) = delete;
    SparseComponentPool(SparseComponentPool&&) noexcept = default;
    SparseComponentPool& operator=(SparseComponentPool&&) noexcept = default;

    [[nodiscard]] bool has(uint32_t entity_index) const noexcept {
        return entity_index < sparse_.size() && sparse_[entity_index] != InvalidUI32;
    }

    [[nodiscard]] T* try_get(uint32_t entity_index) noexcept {
        if (!has(entity_index)) {
            return nullptr;
        }
        return &dense_[sparse_[entity_index]];
    }

    [[nodiscard]] const T* try_get(uint32_t entity_index) const noexcept {
        if (!has(entity_index)) {
            return nullptr;
        }
        return &dense_[sparse_[entity_index]];
    }

    [[nodiscard]] T& get(uint32_t entity_index) {
        OC_ASSERT(has(entity_index));
        return dense_[sparse_[entity_index]];
    }

    [[nodiscard]] const T& get(uint32_t entity_index) const {
        OC_ASSERT(has(entity_index));
        return dense_[sparse_[entity_index]];
    }

    T& add(uint32_t entity_index) {
        if (has(entity_index)) {
            return dense_[sparse_[entity_index]];
        }
        if (entity_index >= sparse_.size()) {
            sparse_.resize(entity_index + 1, InvalidUI32);
        }
        sparse_[entity_index] = static_cast<uint32_t>(dense_.size());
        dense_to_entity_.push_back(entity_index);
        dense_.emplace_back();
        return dense_.back();
    }

    template<typename... Args>
    T& emplace(uint32_t entity_index, Args&&... args) {
        if (has(entity_index)) {
            dense_[sparse_[entity_index]] = T(std::forward<Args>(args)...);
            return dense_[sparse_[entity_index]];
        }
        if (entity_index >= sparse_.size()) {
            sparse_.resize(entity_index + 1, InvalidUI32);
        }
        sparse_[entity_index] = static_cast<uint32_t>(dense_.size());
        dense_to_entity_.push_back(entity_index);
        dense_.emplace_back(std::forward<Args>(args)...);
        return dense_.back();
    }

    bool remove(uint32_t entity_index) {
        if (!has(entity_index)) {
            return false;
        }
        const uint32_t dense_index = sparse_[entity_index];
        const uint32_t last_dense = static_cast<uint32_t>(dense_.size() - 1);
        if (dense_index != last_dense) {
            const uint32_t moved_entity = dense_to_entity_[last_dense];
            dense_[dense_index] = std::move(dense_[last_dense]);
            dense_to_entity_[dense_index] = moved_entity;
            sparse_[moved_entity] = dense_index;
        }
        dense_.pop_back();
        dense_to_entity_.pop_back();
        sparse_[entity_index] = InvalidUI32;
        return true;
    }

    void clear() noexcept {
        dense_.clear();
        dense_to_entity_.clear();
        sparse_.clear();
    }

    [[nodiscard]] size_t size() const noexcept { return dense_.size(); }
    [[nodiscard]] bool empty() const noexcept { return dense_.empty(); }

    [[nodiscard]] T* data() noexcept { return dense_.data(); }
    [[nodiscard]] const T* data() const noexcept { return dense_.data(); }

    [[nodiscard]] dense_vector& dense() noexcept { return dense_; }
    [[nodiscard]] const dense_vector& dense() const noexcept { return dense_; }

    [[nodiscard]] uint32_t entity_at_dense(uint32_t dense_index) const noexcept {
        return dense_to_entity_[dense_index];
    }

    [[nodiscard]] const index_vector& dense_to_entity() const noexcept {
        return dense_to_entity_;
    }

private:
    dense_vector dense_;
    index_vector dense_to_entity_;
    index_vector sparse_;
};

}// namespace ocarina

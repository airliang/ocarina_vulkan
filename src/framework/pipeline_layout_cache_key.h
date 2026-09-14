#pragma once

#include "core/hash.h"
#include "core/util.h"

namespace ocarina {

class ShaderProgram;

struct PipelineLayoutCacheKey {
    ShaderProgram* shader_program = nullptr;

    bool operator==(const PipelineLayoutCacheKey& other) const noexcept {
        return shader_program == other.shader_program;
    }
};

struct PipelineLayoutCacheKeyHash {
    size_t operator()(const PipelineLayoutCacheKey& key) const noexcept {
        size_t hash = 0;
        hash_combine(hash, reinterpret_cast<uintptr_t>(key.shader_program));
        return hash;
    }
};

}// namespace ocarina

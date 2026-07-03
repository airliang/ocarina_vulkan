#pragma once

#include "core/hash.h"
#include "core/util.h"
#include "rhi/pipeline_state.h"

namespace ocarina {

struct PipelineLayoutCacheKey {
    handle_ty shaders[PipelineState::MAX_SHADER_STAGE] = {};

    bool operator==(const PipelineLayoutCacheKey& other) const noexcept {
        return shaders[0] == other.shaders[0] && shaders[1] == other.shaders[1];
    }
};

struct PipelineLayoutCacheKeyHash {
    size_t operator()(const PipelineLayoutCacheKey& key) const noexcept {
        size_t hash = 0;
        hash_combine(hash, key.shaders[0]);
        hash_combine(hash, key.shaders[1]);
        return hash;
    }
};

}// namespace ocarina

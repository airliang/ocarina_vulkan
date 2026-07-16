#pragma once

#include "core/header.h"
#include "core/hash.h"
#include "core/util.h"
#include "rhi/pipeline_state.h"

namespace ocarina {

class RHIRenderPass;

struct PipelineCacheKey {
    PipelineState pipeline_state{};
    RHIRenderPass* render_pass = nullptr;

    bool operator==(const PipelineCacheKey& other) const noexcept {
        return render_pass == other.render_pass
            && pipeline_state == other.pipeline_state;
    }
};

inline PipelineCacheKey MakePipelineCacheKey(
    const PipelineState& pipeline_state,
    RHIRenderPass* render_pass) noexcept {
    return PipelineCacheKey{pipeline_state.ForCacheKey(), render_pass};
}

struct PipelineCacheKeyHash {
    size_t operator()(const PipelineCacheKey& key) const noexcept {
        PipelineStateHash state_hash;
        size_t hash = state_hash(key.pipeline_state);
        hash_combine(hash, reinterpret_cast<uintptr_t>(key.render_pass));
        return hash;
    }
};

}// namespace ocarina

#pragma once

#include "core/header.h"
#include "core/hash.h"
#include "core/util.h"
#include "pipeline_state.h"

namespace ocarina {

class RHIRenderPass;

/// Cache key for graphics PSOs. Vertex inputs are implied by the vertex shader handle.
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
    PipelineCacheKey key{};
    key.pipeline_state = pipeline_state.ForCacheKey();
    key.render_pass = render_pass;
    return key;
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

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "pass_group_id.h"
#include "math/basic_types.h"
#include "rhi/graphics_descriptions.h"

namespace ocarina {

struct PipelineTargetDesc {
    ocarina_string name;
    ocarina_string format;
    float scale = 1.0f;
};

struct PipelineColorAttachmentDesc {
    ocarina_string target;
    PipelineLoadOp load = PipelineLoadOp::Clear;
    PipelineStoreOp store = PipelineStoreOp::Store;
    float4 clear = {0.0f, 0.0f, 0.0f, 1.0f};
    bool has_clear = false;
};

struct PipelineDepthAttachmentDesc {
    ocarina_string target;
    PipelineLoadOp load = PipelineLoadOp::Clear;
    PipelineStoreOp store = PipelineStoreOp::Store;
    float clear = 1.0f;
    bool has_clear = false;
    bool enabled = false;
};

struct RenderPassDesc {
    ocarina_string name;
    PassGroupId group = PassGroupId::Opaque;
    ocarina_string technique_role;
    bool has_technique_role = false;
    ocarina_vector<PipelineColorAttachmentDesc> color;
    PipelineDepthAttachmentDesc depth;
};

struct OC_FRAMEWORK_API RenderPipelineAsset {
    uint32_t version = 0;
    ocarina_string name;
    ocarina_vector<PipelineTargetDesc> targets;
    ocarina_vector<RenderPassDesc> passes;

    /// Load a RenderPipeline JSON (or JSONC) file into this asset.
    /// Returns false on IO / parse / validation failure (errors logged).
    bool load_pipeline_asset(const fs::path& path);

    [[nodiscard]] bool valid() const noexcept {
        return version > 0 && !name.empty() && !passes.empty();
    }

    [[nodiscard]] const RenderPassDesc* find_pass(std::string_view pass_name) const noexcept {
        for (const RenderPassDesc& pass : passes) {
            if (pass.name == pass_name) {
                return &pass;
            }
        }
        return nullptr;
    }
};

}// namespace ocarina

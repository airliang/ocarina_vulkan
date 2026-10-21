#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "math/basic_types.h"
#include "pass_group_id.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/pipeline_state.h"

namespace ocarina {

class RHIRenderPass;
class RenderPipelineAsset;
class Renderer;

enum class MaterialPropertyValueType : uint8_t {
    Float = 0,
    Float4,
    Texture2D,
};

enum class MaterialBlendMode : uint8_t {
    Opaque = 0,
    Alpha,
    Additive,
};

struct MaterialPropertyDesc {
    ocarina_string name;
    MaterialPropertyValueType type = MaterialPropertyValueType::Float;
    float float_value = 0.0f;
    float4 float4_value = {0.0f, 0.0f, 0.0f, 0.0f};
    ocarina_string texture_path;
    bool texture_srgb = true;
    bool has_value = false;
};

struct MaterialTechniqueOverrides {
    bool color_write = true;
    bool has_color_write = false;
    bool depth_write = true;
    bool has_depth_write = false;
    SamplerCompareFunc depth_compare = SamplerCompareFunc::LE;
    bool has_depth_compare = false;
    CullingMode cull_mode = CullingMode::BACK;
    bool has_cull_mode = false;
    MaterialBlendMode blend = MaterialBlendMode::Opaque;
    bool has_blend = false;
};

/// One shading technique (shader pair + macros + optional state overrides).
/// Techniques in the same MaterialAsset form a shader family: see MaterialAsset docs
/// for descriptor-set layout sharing and (set, name) → binding consistency rules.
struct MaterialTechniqueDesc {
    ocarina_string name;
    ocarina_string vertex_shader;
    ocarina_string pixel_shader;
    ocarina_vector<ocarina_string> macros;
    MaterialTechniqueOverrides overrides;
    /// Resolved at bind time from RenderPipelineAsset / Renderer.
    RHIRenderPass* render_pass = nullptr;
};

/// Authoring rules for techniques that share one Material instance:
/// - All techniques bind material resources on the same descriptor set index
///   (MATERIAL_SET = 1). FRAME_SET (0) is engine-owned and not authored per technique.
/// - For a given set, a binding name must map to the same binding index (and type)
///   in every technique that declares it. Conflicts are reported when the material
///   is initialized (after shader reflection), not only from JSON parsing.
/// - A technique may declare a subset of the family's bindings (e.g. DepthOnly only
///   needs material_ubo). At runtime the Material merges the union of all technique
///   bindings into one shared VkDescriptorSetLayout and assigns it to every program
///   so a single material descriptor set can be bound for all passes.
/// - Prefer aligning material_ubo at binding 0 across the family.
struct OC_FRAMEWORK_API MaterialAsset {
    uint32_t version = 0;
    ocarina_string name;
    ocarina_string shading_model;
    ocarina_string template_name;
    ocarina_vector<ocarina_string> features;
    ocarina_vector<MaterialPropertyDesc> properties;
    ocarina_vector<MaterialTechniqueDesc> techniques;
    /// Pass name (or PassGroupId name) → technique name.
    ocarina_map<ocarina_string, ocarina_string> pass_participation;

    /// Load a Material JSON (or JSONC) file into this asset.
    bool load_material_asset(const fs::path& path);

    [[nodiscard]] bool valid() const noexcept {
        return version > 0 && !techniques.empty();
    }

    [[nodiscard]] const MaterialTechniqueDesc* find_technique(std::string_view technique_name) const noexcept;
    [[nodiscard]] MaterialTechniqueDesc* find_technique(std::string_view technique_name) noexcept;

    /// Technique used for the given pass name via pass_participation.
    [[nodiscard]] const MaterialTechniqueDesc* technique_for_pass(std::string_view pass_name) const noexcept;

    /// Prefer ForwardOpaque / ForwardTransparent / first technique.
    [[nodiscard]] const MaterialTechniqueDesc* primary_technique() const noexcept;

    /// Resolve technique render_pass pointers from a loaded Renderer pipeline.
    void bind_render_passes(const Renderer& renderer);

    /// Resolve technique render_pass using pass_participation + an explicit name→pass map.
    void bind_render_pass(std::string_view pass_name, RHIRenderPass* render_pass);

    [[nodiscard]] ocarina_set<ocarina_string> technique_pixel_options(
        const MaterialTechniqueDesc& technique) const;

    void apply_technique_overrides_to_pipeline(
        const MaterialTechniqueDesc& technique,
        PipelineState& pipeline_state) const;
};

}// namespace ocarina

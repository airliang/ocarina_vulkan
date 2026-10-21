#include "material_asset.h"

#include "core/logging.h"
#include "core/string_util.h"
#include "renderer.h"
#include "rhi/shader_program_key.h"

#include <fstream>
#include <optional>
#include <string>
#include "ext/nlohmann/json.hpp"

namespace ocarina {

namespace {

using json = nlohmann::json;

[[nodiscard]] ocarina_string to_oc_string(const std::string& s) {
    return ocarina_string(s.data(), s.size());
}

[[nodiscard]] bool read_file_text(const fs::path& path, ocarina_string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0) {
        file.read(out.data(), size);
    }
    return static_cast<bool>(file) || size == 0;
}

[[nodiscard]] std::optional<SamplerCompareFunc> parse_depth_compare(std::string_view s) {
    if (s == "Never") return SamplerCompareFunc::N;
    if (s == "Less") return SamplerCompareFunc::L;
    if (s == "Equal") return SamplerCompareFunc::E;
    if (s == "LessOrEqual") return SamplerCompareFunc::LE;
    if (s == "Greater") return SamplerCompareFunc::G;
    if (s == "NotEqual") return SamplerCompareFunc::NE;
    if (s == "GreaterOrEqual") return SamplerCompareFunc::GE;
    if (s == "Always") return SamplerCompareFunc::A;
    return std::nullopt;
}

[[nodiscard]] std::optional<CullingMode> parse_cull_mode(std::string_view s) {
    if (s == "None") return CullingMode::NONE;
    if (s == "Front") return CullingMode::FRONT;
    if (s == "Back") return CullingMode::BACK;
    if (s == "FrontAndBack") return CullingMode::FRONT_AND_BACK;
    return std::nullopt;
}

[[nodiscard]] std::optional<MaterialBlendMode> parse_blend_mode(std::string_view s) {
    if (s == "Opaque") return MaterialBlendMode::Opaque;
    if (s == "Alpha") return MaterialBlendMode::Alpha;
    if (s == "Additive") return MaterialBlendMode::Additive;
    return std::nullopt;
}

[[nodiscard]] bool parse_property(const ocarina_string& name, const json& j, MaterialPropertyDesc& out) {
    if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) {
        return false;
    }
    out.name = name;
    const std::string type = j["type"].get<std::string>();
    if (type == "float") {
        out.type = MaterialPropertyValueType::Float;
        if (j.contains("value") && j["value"].is_number()) {
            out.float_value = j["value"].get<float>();
            out.has_value = true;
        }
        return true;
    }
    if (type == "float4") {
        out.type = MaterialPropertyValueType::Float4;
        if (j.contains("value") && j["value"].is_array() && j["value"].size() == 4) {
            out.float4_value = make_float4(
                j["value"][0].get<float>(),
                j["value"][1].get<float>(),
                j["value"][2].get<float>(),
                j["value"][3].get<float>());
            out.has_value = true;
        }
        return true;
    }
    if (type == "texture2d") {
        out.type = MaterialPropertyValueType::Texture2D;
        if (j.contains("path") && j["path"].is_string()) {
            out.texture_path = to_oc_string(j["path"].get<std::string>());
            out.has_value = true;
        }
        if (j.contains("colorspace") && j["colorspace"].is_string()) {
            const std::string cs = j["colorspace"].get<std::string>();
            out.texture_srgb = (cs == "srgb" || cs == "SRGB");
        }
        return true;
    }
    return false;
}

[[nodiscard]] bool parse_overrides(const json& j, MaterialTechniqueOverrides& out) {
    if (!j.is_object()) {
        return false;
    }
    if (j.contains("color_write")) {
        if (!j["color_write"].is_boolean()) {
            return false;
        }
        out.color_write = j["color_write"].get<bool>();
        out.has_color_write = true;
    }
    if (j.contains("depth_write")) {
        if (!j["depth_write"].is_boolean()) {
            return false;
        }
        out.depth_write = j["depth_write"].get<bool>();
        out.has_depth_write = true;
    }
    if (j.contains("depth_compare")) {
        if (!j["depth_compare"].is_string()) {
            return false;
        }
        auto cmp = parse_depth_compare(j["depth_compare"].get<std::string>());
        if (!cmp) {
            return false;
        }
        out.depth_compare = *cmp;
        out.has_depth_compare = true;
    }
    if (j.contains("cull_mode")) {
        if (!j["cull_mode"].is_string()) {
            return false;
        }
        auto cull = parse_cull_mode(j["cull_mode"].get<std::string>());
        if (!cull) {
            return false;
        }
        out.cull_mode = *cull;
        out.has_cull_mode = true;
    }
    if (j.contains("blend")) {
        if (!j["blend"].is_string()) {
            return false;
        }
        auto blend = parse_blend_mode(j["blend"].get<std::string>());
        if (!blend) {
            return false;
        }
        out.blend = *blend;
        out.has_blend = true;
    }
    return true;
}

[[nodiscard]] bool parse_technique(const ocarina_string& name, const json& j, MaterialTechniqueDesc& out) {
    if (!j.is_object()) {
        return false;
    }
    out.name = name;
    if (!j.contains("shaders") || !j["shaders"].is_object()) {
        return false;
    }
    const json& shaders = j["shaders"];
    if (!shaders.contains("vertex") || !shaders["vertex"].is_string()
        || !shaders.contains("pixel") || !shaders["pixel"].is_string()) {
        return false;
    }
    out.vertex_shader = to_oc_string(shaders["vertex"].get<std::string>());
    out.pixel_shader = to_oc_string(shaders["pixel"].get<std::string>());

    if (j.contains("macros")) {
        if (!j["macros"].is_array()) {
            return false;
        }
        for (const auto& macro_j : j["macros"]) {
            if (!macro_j.is_string()) {
                return false;
            }
            out.macros.push_back(to_oc_string(macro_j.get<std::string>()));
        }
    }

    if (j.contains("overrides")) {
        if (!parse_overrides(j["overrides"], out.overrides)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<MaterialAsset> parse_material_json(const json& root) {
    if (!root.is_object()) {
        OC_ERROR("MaterialAsset: root must be an object");
        return std::nullopt;
    }
    if (!root.contains("version") || !root["version"].is_number_integer()) {
        OC_ERROR("MaterialAsset: missing integer 'version'");
        return std::nullopt;
    }

    MaterialAsset asset;
    asset.version = root["version"].get<uint32_t>();
    if (asset.version < 1) {
        OC_ERROR_FORMAT("MaterialAsset: unsupported version {}", asset.version);
        return std::nullopt;
    }

    if (root.contains("name") && root["name"].is_string()) {
        asset.name = to_oc_string(root["name"].get<std::string>());
    }
    if (root.contains("shading_model") && root["shading_model"].is_string()) {
        asset.shading_model = to_oc_string(root["shading_model"].get<std::string>());
    }
    if (root.contains("template") && root["template"].is_string()) {
        asset.template_name = to_oc_string(root["template"].get<std::string>());
    }

    if (root.contains("features")) {
        if (!root["features"].is_array()) {
            return std::nullopt;
        }
        for (const auto& feature_j : root["features"]) {
            if (!feature_j.is_string()) {
                return std::nullopt;
            }
            asset.features.push_back(to_oc_string(feature_j.get<std::string>()));
        }
    }

    if (root.contains("properties")) {
        if (!root["properties"].is_object()) {
            return std::nullopt;
        }
        for (auto it = root["properties"].begin(); it != root["properties"].end(); ++it) {
            MaterialPropertyDesc prop;
            if (!parse_property(to_oc_string(it.key()), it.value(), prop)) {
                OC_ERROR_FORMAT("MaterialAsset: invalid property '{}'", it.key());
                return std::nullopt;
            }
            asset.properties.push_back(std::move(prop));
        }
    }

    if (!root.contains("techniques") || !root["techniques"].is_object() || root["techniques"].empty()) {
        OC_ERROR("MaterialAsset: 'techniques' must be a non-empty object");
        return std::nullopt;
    }
    for (auto it = root["techniques"].begin(); it != root["techniques"].end(); ++it) {
        MaterialTechniqueDesc technique;
        if (!parse_technique(to_oc_string(it.key()), it.value(), technique)) {
            OC_ERROR_FORMAT("MaterialAsset: invalid technique '{}'", it.key());
            return std::nullopt;
        }
        asset.techniques.push_back(std::move(technique));
    }

    if (root.contains("pass_participation")) {
        if (!root["pass_participation"].is_object()) {
            return std::nullopt;
        }
        for (auto it = root["pass_participation"].begin(); it != root["pass_participation"].end(); ++it) {
            if (!it.value().is_string()) {
                return std::nullopt;
            }
            asset.pass_participation[to_oc_string(it.key())] = to_oc_string(it.value().get<std::string>());
        }
    }

    return asset;
}

}// namespace

const MaterialTechniqueDesc* MaterialAsset::find_technique(std::string_view technique_name) const noexcept {
    for (const MaterialTechniqueDesc& technique : techniques) {
        if (technique.name == technique_name) {
            return &technique;
        }
    }
    return nullptr;
}

MaterialTechniqueDesc* MaterialAsset::find_technique(std::string_view technique_name) noexcept {
    for (MaterialTechniqueDesc& technique : techniques) {
        if (technique.name == technique_name) {
            return &technique;
        }
    }
    return nullptr;
}

const MaterialTechniqueDesc* MaterialAsset::technique_for_pass(std::string_view pass_name) const noexcept {
    const ocarina_string key(pass_name.data(), pass_name.size());
    const auto it = pass_participation.find(key);
    if (it == pass_participation.end()) {
        return nullptr;
    }
    return find_technique(it->second);
}

const MaterialTechniqueDesc* MaterialAsset::primary_technique() const noexcept {
    if (const MaterialTechniqueDesc* tech = find_technique("ForwardOpaque")) {
        return tech;
    }
    if (const MaterialTechniqueDesc* tech = find_technique("ForwardTransparent")) {
        return tech;
    }
    return techniques.empty() ? nullptr : &techniques.front();
}

void MaterialAsset::bind_render_pass(std::string_view pass_name, RHIRenderPass* render_pass) {
    if (render_pass == nullptr) {
        return;
    }
    if (MaterialTechniqueDesc* technique = const_cast<MaterialTechniqueDesc*>(technique_for_pass(pass_name))) {
        technique->render_pass = render_pass;
    }
}

void MaterialAsset::bind_render_passes(const Renderer& renderer) {
    for (const auto& [pass_name, technique_name] : pass_participation) {
        (void)technique_name;
        if (RHIRenderPass* pass = renderer.find_pipeline_render_pass(pass_name)) {
            bind_render_pass(pass_name, pass);
        }
    }
}

ocarina_set<ocarina_string> MaterialAsset::technique_pixel_options(
    const MaterialTechniqueDesc& technique) const {
    ocarina_set<ocarina_string> options;
    for (const ocarina_string& macro : technique.macros) {
        if (macro.find('=') != ocarina_string::npos) {
            options.insert(macro);
        } else {
            options.insert(make_shader_option(macro.c_str(), 1));
        }
    }
    return options;
}

void MaterialAsset::apply_technique_overrides_to_pipeline(
    const MaterialTechniqueDesc& technique,
    PipelineState& pipeline_state) const {
    const MaterialTechniqueOverrides& overrides = technique.overrides;
    if (overrides.has_cull_mode) {
        pipeline_state.raster_state.cull_mode = overrides.cull_mode;
    }
    if (overrides.has_depth_write) {
        pipeline_state.depth_stencil_state.depth_write_enable = overrides.depth_write;
    }
    if (overrides.has_depth_compare) {
        pipeline_state.depth_stencil_state.depth_compare_op = overrides.depth_compare;
    }
    if (overrides.has_blend) {
        switch (overrides.blend) {
            case MaterialBlendMode::Alpha:
                pipeline_state.blend_state = BlendState::AlphaBlend();
                pipeline_state.depth_stencil_state.depth_write_enable = false;
                break;
            case MaterialBlendMode::Additive: {
                BlendState additive = BlendState::Opaque();
                additive.blend_enable = true;
                additive.srccolorblend_factor = BlendFunction::SRC_ALPHA;
                additive.dstcolorblend_factor = BlendFunction::ONE;
                additive.srcalphablend_factor = BlendFunction::SRC_ALPHA;
                additive.dstalphablend_factor = BlendFunction::ONE;
                pipeline_state.blend_state = additive;
                pipeline_state.depth_stencil_state.depth_write_enable = false;
                break;
            }
            case MaterialBlendMode::Opaque:
            default:
                pipeline_state.blend_state = BlendState::Opaque();
                break;
        }
    }
    if (overrides.has_color_write) {
        pipeline_state.blend_state.color_mask = overrides.color_write
            ? ColorMask::ColorMaskAll
            : ColorMask::ColorMaskNone;
    }
}

bool MaterialAsset::load_material_asset(const fs::path& path) {
    ocarina_string text;
    if (!read_file_text(path, text)) {
        OC_ERROR_FORMAT("MaterialAsset: failed to read '{}'", path.string());
        return false;
    }

    const ocarina_string json_text = jsonc_to_json(text);
    json root;
    try {
        root = json::parse(json_text.c_str());
    } catch (const json::parse_error& e) {
        OC_ERROR_FORMAT("MaterialAsset: JSON parse error in '{}': {}", path.string(), e.what());
        return false;
    }

    auto parsed = parse_material_json(root);
    if (!parsed || !parsed->valid()) {
        OC_ERROR_FORMAT("MaterialAsset: validation failed for '{}'", path.string());
        return false;
    }

    *this = std::move(*parsed);
    return true;
}

}// namespace ocarina

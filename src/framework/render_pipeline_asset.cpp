#include "render_pipeline_asset.h"
#include "core/logging.h"
#include "core/string_util.h"

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

[[nodiscard]] std::optional<PipelineLoadOp> parse_load_op(std::string_view s) {
    if (s == "Clear") return PipelineLoadOp::Clear;
    if (s == "Load") return PipelineLoadOp::Load;
    if (s == "DontCare") return PipelineLoadOp::DontCare;
    return std::nullopt;
}

[[nodiscard]] std::optional<PipelineStoreOp> parse_store_op(std::string_view s) {
    if (s == "Store") return PipelineStoreOp::Store;
    if (s == "DontCare") return PipelineStoreOp::DontCare;
    return std::nullopt;
}

[[nodiscard]] bool parse_color_attachment(const json& j, PipelineColorAttachmentDesc& out) {
    if (!j.is_object() || !j.contains("target") || !j["target"].is_string()) {
        return false;
    }
    out.target = to_oc_string(j["target"].get<std::string>());
    if (j.contains("load")) {
        if (!j["load"].is_string()) {
            return false;
        }
        auto load = parse_load_op(j["load"].get<std::string>());
        if (!load) {
            return false;
        }
        out.load = *load;
    }
    if (j.contains("store")) {
        if (!j["store"].is_string()) {
            return false;
        }
        auto store = parse_store_op(j["store"].get<std::string>());
        if (!store) {
            return false;
        }
        out.store = *store;
    }
    if (j.contains("clear")) {
        if (!j["clear"].is_array() || j["clear"].size() != 4) {
            return false;
        }
        out.clear = make_float4(
            j["clear"][0].get<float>(),
            j["clear"][1].get<float>(),
            j["clear"][2].get<float>(),
            j["clear"][3].get<float>());
        out.has_clear = true;
    }
    return true;
}

[[nodiscard]] bool parse_depth_attachment(const json& j, PipelineDepthAttachmentDesc& out) {
    if (!j.is_object() || !j.contains("target") || !j["target"].is_string()) {
        return false;
    }
    out.enabled = true;
    out.target = to_oc_string(j["target"].get<std::string>());
    if (j.contains("load")) {
        if (!j["load"].is_string()) {
            return false;
        }
        auto load = parse_load_op(j["load"].get<std::string>());
        if (!load) {
            return false;
        }
        out.load = *load;
    }
    if (j.contains("store")) {
        if (!j["store"].is_string()) {
            return false;
        }
        auto store = parse_store_op(j["store"].get<std::string>());
        if (!store) {
            return false;
        }
        out.store = *store;
    }
    if (j.contains("clear")) {
        if (!j["clear"].is_number()) {
            return false;
        }
        out.clear = j["clear"].get<float>();
        out.has_clear = true;
    }
    return true;
}

[[nodiscard]] bool parse_pass(const json& j, RenderPassDesc& out) {
    if (!j.is_object() || !j.contains("name") || !j["name"].is_string()
        || !j.contains("group") || !j["group"].is_string()) {
        return false;
    }
    out.name = to_oc_string(j["name"].get<std::string>());
    auto group = pass_group_id_from_name(j["group"].get<std::string>());
    if (!group) {
        OC_ERROR_FORMAT("RenderPipeline: unknown pass group '{}'", j["group"].get<std::string>());
        return false;
    }
    out.group = *group;

    if (j.contains("technique_role") && !j["technique_role"].is_null()) {
        if (!j["technique_role"].is_string()) {
            return false;
        }
        out.technique_role = to_oc_string(j["technique_role"].get<std::string>());
        out.has_technique_role = true;
    }

    if (j.contains("color")) {
        if (!j["color"].is_array()) {
            return false;
        }
        for (const auto& color_j : j["color"]) {
            PipelineColorAttachmentDesc color;
            if (!parse_color_attachment(color_j, color)) {
                return false;
            }
            out.color.push_back(std::move(color));
        }
    }

    if (j.contains("depth") && !j["depth"].is_null()) {
        if (!parse_depth_attachment(j["depth"], out.depth)) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] std::optional<RenderPipelineAsset> parse_pipeline_json(const json& root) {
    if (!root.is_object()) {
        OC_ERROR("RenderPipeline: root must be an object");
        return std::nullopt;
    }
    if (!root.contains("version") || !root["version"].is_number_integer()) {
        OC_ERROR("RenderPipeline: missing integer 'version'");
        return std::nullopt;
    }
    if (!root.contains("name") || !root["name"].is_string()) {
        OC_ERROR("RenderPipeline: missing string 'name'");
        return std::nullopt;
    }
    if (!root.contains("passes") || !root["passes"].is_array() || root["passes"].empty()) {
        OC_ERROR("RenderPipeline: 'passes' must be a non-empty array");
        return std::nullopt;
    }

    RenderPipelineAsset asset;
    asset.version = root["version"].get<uint32_t>();
    if (asset.version < 1) {
        OC_ERROR_FORMAT("RenderPipeline: unsupported version {}", asset.version);
        return std::nullopt;
    }
    asset.name = to_oc_string(root["name"].get<std::string>());

    if (root.contains("targets")) {
        if (!root["targets"].is_object()) {
            OC_ERROR("RenderPipeline: 'targets' must be an object");
            return std::nullopt;
        }
        for (auto it = root["targets"].begin(); it != root["targets"].end(); ++it) {
            if (!it.value().is_object() || !it.value().contains("format")
                || !it.value()["format"].is_string()) {
                OC_ERROR_FORMAT("RenderPipeline: invalid target '{}'", it.key());
                return std::nullopt;
            }
            PipelineTargetDesc target;
            target.name = to_oc_string(it.key());
            target.format = to_oc_string(it.value()["format"].get<std::string>());
            if (it.value().contains("scale")) {
                if (!it.value()["scale"].is_number()) {
                    return std::nullopt;
                }
                target.scale = it.value()["scale"].get<float>();
            }
            asset.targets.push_back(std::move(target));
        }
    }

    for (const auto& pass_j : root["passes"]) {
        RenderPassDesc pass;
        if (!parse_pass(pass_j, pass)) {
            OC_ERROR("RenderPipeline: failed to parse a pass entry");
            return std::nullopt;
        }
        asset.passes.push_back(std::move(pass));
    }

    return asset;
}

}// namespace

bool RenderPipelineAsset::load_pipeline_asset(const fs::path& path) {
    ocarina_string text;
    if (!read_file_text(path, text)) {
        OC_ERROR_FORMAT("RenderPipeline: failed to read '{}'", path.string());
        return false;
    }

    const ocarina_string json_text = jsonc_to_json(text);
    json root;
    try {
        root = json::parse(json_text.c_str());
    } catch (const json::parse_error& e) {
        OC_ERROR_FORMAT("RenderPipeline: JSON parse error in '{}': {}", path.string(), e.what());
        return false;
    }

    auto parsed = parse_pipeline_json(root);
    if (!parsed || !parsed->valid()) {
        OC_ERROR_FORMAT("RenderPipeline: validation failed for '{}'", path.string());
        return false;
    }

    *this = std::move(*parsed);
    return true;
}

}// namespace ocarina

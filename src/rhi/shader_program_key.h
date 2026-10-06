#pragma once

#include "core/hash.h"
#include "core/stl.h"
#include "core/util.h"
#include "rhi/graphics_descriptions.h"

namespace ocarina {

/// DXC -D style option string, e.g. make_shader_option("ALPHA_BLEND", 1) → "ALPHA_BLEND=1".
[[nodiscard]] inline std::string make_shader_option(const char* name, int value) {
    return std::string(name) + "=" + std::to_string(value);
}

/// Stable 64-bit identity for one compiled shader stage (file + type + entry + options).
[[nodiscard]] inline uint64_t compute_shader_stage_hash(
    std::string_view filename,
    ShaderType shader_type,
    std::string_view entry_point,
    const std::set<std::string>& options) noexcept {
    uint64_t options_hash = Hash64::default_seed;
    for (const std::string& option : options) {
        options_hash = detail::hash64(option, options_hash);
    }
    return hash64(
        filename,
        static_cast<uint32_t>(shader_type),
        entry_point,
        options_hash);
}

/// Hex display of a stage hash (uppercase, 16 chars) — used as SPIR-V cache file stem.
[[nodiscard]] inline std::string shader_stage_hash_hex(uint64_t hash) {
    return std::string{hash_to_string(hash)};
}

/// Cache key for a compiled graphics or compute shader program.
/// Same shader file + different option sets → distinct programs (and SPIR-V caches).
struct ShaderProgramKey {
    std::string vertex_shader_file;
    std::string pixel_shader_file;
    std::string compute_shader_file;
    std::set<std::string> vertex_options;
    std::set<std::string> pixel_options;
    std::set<std::string> compute_options;
    std::string entry_point = "main";

    [[nodiscard]] bool is_graphics() const noexcept {
        return !vertex_shader_file.empty() && !pixel_shader_file.empty();
    }

    [[nodiscard]] bool is_compute() const noexcept {
        return !compute_shader_file.empty();
    }

    [[nodiscard]] uint64_t vertex_stage_hash() const noexcept {
        return compute_shader_stage_hash(
            vertex_shader_file,
            ShaderType::VertexShader,
            entry_point,
            vertex_options);
    }

    [[nodiscard]] uint64_t pixel_stage_hash() const noexcept {
        return compute_shader_stage_hash(
            pixel_shader_file,
            ShaderType::PixelShader,
            entry_point,
            pixel_options);
    }

    [[nodiscard]] uint64_t compute_stage_hash() const noexcept {
        return compute_shader_stage_hash(
            compute_shader_file,
            ShaderType::ComputeShader,
            entry_point,
            compute_options);
    }

    bool operator==(const ShaderProgramKey& other) const {
        return vertex_shader_file == other.vertex_shader_file
            && pixel_shader_file == other.pixel_shader_file
            && compute_shader_file == other.compute_shader_file
            && vertex_options == other.vertex_options
            && pixel_options == other.pixel_options
            && compute_options == other.compute_options
            && entry_point == other.entry_point;
    }
};

struct HashShaderProgramKeyFunction {
    uint64_t operator()(const ShaderProgramKey& key) const {
        std::size_t res = 0;
        hash_combine(res, std::hash<std::string>()(key.vertex_shader_file));
        hash_combine(res, std::hash<std::string>()(key.pixel_shader_file));
        hash_combine(res, std::hash<std::string>()(key.compute_shader_file));
        hash_combine(res, std::hash<std::string>()(key.entry_point));
        for (const std::string& option : key.vertex_options) {
            hash_combine(res, std::hash<std::string>()(option));
        }
        for (const std::string& option : key.pixel_options) {
            hash_combine(res, std::hash<std::string>()(option));
        }
        for (const std::string& option : key.compute_options) {
            hash_combine(res, std::hash<std::string>()(option));
        }
        return res;
    }
};

} // namespace ocarina

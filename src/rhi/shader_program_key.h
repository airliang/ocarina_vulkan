#pragma once

#include "core/hash.h"
#include "core/stl.h"

namespace ocarina {

/// Cache key for a compiled graphics or compute shader program.
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

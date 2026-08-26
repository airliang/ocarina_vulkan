#pragma once

#include "core/header.h"
#include "core/hash.h"
#include "core/stl.h"
#include "rhi/pipeline_cache_key.h"
#include "rhi/pipeline_state.h"

namespace ocarina {

class RHIRenderPass;

/// File- or handle-based request to compile / cache a graphics PSO.
/// Vertex inputs are defined by the vertex shader (and its options), not stored here.
struct PSORequest {
    std::string vertex_shader_path;
    std::string pixel_shader_path;
    std::set<string> vertex_options;
    std::set<string> pixel_options;

    handle_ty vertex_shader = InvalidUI64;
    handle_ty pixel_shader = InvalidUI64;

    RHIRenderPass* render_pass = nullptr;

    RasterState raster_state = RasterState::Default();
    BlendState blend_state = BlendState::Opaque();
    DepthStencilState depth_stencil_state = DepthStencilState::Default();
    MultiSampleState multi_sample_state{};
    PrimitiveType primitive_type = PrimitiveType::TRIANGLES;

    [[nodiscard]] static PSORequest make_graphics(
        std::string vertex_path,
        std::string pixel_path,
        RHIRenderPass* render_pass,
        std::set<string> vertex_options = {},
        std::set<string> pixel_options = {}) {
        PSORequest request;
        request.vertex_shader_path = std::move(vertex_path);
        request.pixel_shader_path = std::move(pixel_path);
        request.vertex_options = std::move(vertex_options);
        request.pixel_options = std::move(pixel_options);
        request.render_pass = render_pass;
        return request;
    }

    [[nodiscard]] static PSORequest from_pipeline_state(
        const PipelineState& pipeline_state,
        RHIRenderPass* render_pass) noexcept {
        PSORequest request;
        request.vertex_shader = pipeline_state.shaders[0];
        request.pixel_shader = pipeline_state.shaders[1];
        request.render_pass = render_pass;
        request.raster_state = pipeline_state.raster_state;
        request.blend_state = pipeline_state.blend_state;
        request.depth_stencil_state = pipeline_state.depth_stencil_state;
        request.multi_sample_state = pipeline_state.multiple_sample_state;
        request.primitive_type = pipeline_state.primitive_type;
        return request;
    }

    [[nodiscard]] bool has_shader_paths() const noexcept {
        return !vertex_shader_path.empty() && !pixel_shader_path.empty();
    }

    [[nodiscard]] bool has_shader_handles() const noexcept {
        return vertex_shader != 0 && vertex_shader != InvalidUI64
            && pixel_shader != 0 && pixel_shader != InvalidUI64;
    }

    [[nodiscard]] PipelineState make_pipeline_state() const noexcept {
        PipelineState state = PipelineState::MakeGraphicsDefault(vertex_shader, pixel_shader);
        state.raster_state = raster_state;
        state.blend_state = blend_state;
        state.depth_stencil_state = depth_stencil_state;
        state.multiple_sample_state = multi_sample_state;
        state.primitive_type = primitive_type;
        return state;
    }

    [[nodiscard]] PipelineCacheKey make_cache_key() const noexcept {
        return MakePipelineCacheKey(make_pipeline_state(), render_pass);
    }

    /// Identity for the pending request queue (ignore resolved handles).
    [[nodiscard]] bool identity_equals(const PSORequest& other) const noexcept {
        if (render_pass != other.render_pass
            || primitive_type != other.primitive_type
            || raster_state != other.raster_state
            || blend_state != other.blend_state
            || depth_stencil_state != other.depth_stencil_state) {
            return false;
        }
        if (has_shader_paths() || other.has_shader_paths()) {
            return vertex_shader_path == other.vertex_shader_path
                && pixel_shader_path == other.pixel_shader_path
                && vertex_options == other.vertex_options
                && pixel_options == other.pixel_options;
        }
        return vertex_shader == other.vertex_shader
            && pixel_shader == other.pixel_shader;
    }
};

struct PSORequestHash {
    size_t operator()(const PSORequest& request) const noexcept {
        size_t hash = 0;
        hash_combine(hash, reinterpret_cast<uintptr_t>(request.render_pass));
        hash_combine(hash, static_cast<uint32_t>(request.primitive_type));
        if (request.has_shader_paths()) {
            hash_combine(hash, std::hash<std::string>{}(request.vertex_shader_path));
            hash_combine(hash, std::hash<std::string>{}(request.pixel_shader_path));
            for (const std::string& option : request.vertex_options) {
                hash_combine(hash, std::hash<std::string>{}(option));
            }
            for (const std::string& option : request.pixel_options) {
                hash_combine(hash, std::hash<std::string>{}(option));
            }
        } else {
            hash_combine(hash, request.vertex_shader);
            hash_combine(hash, request.pixel_shader);
        }
        return hash;
    }
};

struct PSORequestIdentityEqual {
    bool operator()(const PSORequest& lhs, const PSORequest& rhs) const noexcept {
        return lhs.identity_equals(rhs);
    }
};

}// namespace ocarina

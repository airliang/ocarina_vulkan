#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "bounding_box.h"
#include "mesh_geometry.h"
#include "rhi/resources/resource.h"

namespace ocarina {

class Device;

class Mesh : public RHIResource {
public:
    Mesh();
    ~Mesh() override;

    static Mesh* create_quad();
    static Mesh* create_cube();
    static Mesh* create_sphere();

    [[nodiscard]] uint32_t mesh_id() const noexcept { return mesh_id_; }

    void set_geometry_slice(const MeshGeometrySlice& slice) { geometry_slice_ = slice; }
    [[nodiscard]] const MeshGeometrySlice& geometry_slice() const { return geometry_slice_; }

    void set_local_bounds(const float3& min_point, const float3& max_point) noexcept;
    [[nodiscard]] bool has_local_bounds() const noexcept { return local_bounds_.valid; }
    [[nodiscard]] const BoundingBox& get_local_bounds() const noexcept { return local_bounds_; }

protected:
    BoundingBox local_bounds_;
    MeshGeometrySlice geometry_slice_{};
    uint32_t mesh_id_ = InvalidUI32;
};

class Quad : public Mesh {
public:
    Quad();
    ~Quad() override;
};

class Cube : public Mesh {
public:
    Cube();
    ~Cube() override;
};

class Sphere : public Mesh {
public:
    explicit Sphere(uint32_t slice_count = 32, uint32_t stack_count = 16);
    ~Sphere() override;
};

}// namespace ocarina

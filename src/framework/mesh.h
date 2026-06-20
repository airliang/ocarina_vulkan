#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "bounding_box.h"
#include "mesh_geometry.h"

namespace ocarina {

class Device;

class Mesh {
public:
    Mesh() = default;
    virtual ~Mesh() = default;

    static Mesh* create_quad();
    static Mesh* create_cube();
    static Mesh* create_sphere();

    void set_geometry_slice(const MeshGeometrySlice& slice) { geometry_slice_ = slice; }
    [[nodiscard]] const MeshGeometrySlice& geometry_slice() const { return geometry_slice_; }

    void set_local_bounds(const float3& min_point, const float3& max_point) noexcept;
    [[nodiscard]] bool has_local_bounds() const noexcept { return local_bounds_.valid; }
    [[nodiscard]] const BoundingBox& get_local_bounds() const noexcept { return local_bounds_; }

protected:
    BoundingBox local_bounds_;
    MeshGeometrySlice geometry_slice_{};
};

constexpr uint32_t MAX_BUILDIN_MESH = 3;

class BuildinMesh : public concepts::Noncopyable {
public:
    enum BuildinMeshType {
        Quad = 0,
        Cube = 1,
        Sphere = 2,
        Custom = MAX_BUILDIN_MESH
    };
public:
    BuildinMesh() = default;
    ~BuildinMesh() {}
    static BuildinMesh& instance()
    {
        static BuildinMesh s_instance;
        return s_instance;
    }

    void create_buildin_mesh(Device* device);
    void cleanup();
    bool is_created() const { return is_created_; }
private:
    bool is_created_ = false;
};

class Quad : public Mesh {
public:
    Quad();
    ~Quad();
};

class Cube : public Mesh {
public:
    Cube();
    ~Cube();
};

class Sphere : public Mesh {
public:
    explicit Sphere(uint32_t slice_count = 32, uint32_t stack_count = 16);
    ~Sphere();
};

}// namespace ocarina

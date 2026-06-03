//
// Created by Zero on 15/09/2022.
//

#pragma once

#include "math/basic_types.h"
#include "dsl/dsl.h"
#include "core/concepts.h"
#include "math/box.h"
#include "quaternion.h"

namespace ocarina {

inline namespace transform {

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] constexpr matrix4_t<T, 4> translation(const T &t) noexcept {
    return make_float4x4(
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        t.x, t.y, t.z, 1.f);
}

template<typename T>
requires is_scalar_expr_v<T>
[[nodiscard]] constexpr matrix4_t<T, 4> translation(const T &x, const T &y, const T &z) noexcept {
    return translation(make_float3(x, y, z));
}

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] constexpr matrix4_t<T, 4> scale(const T &s) noexcept {
    return make_float4x4(
        s.x, 0.f, 0.f, 0.f,
        0.f, s.y, 0.f, 0.f,
        0.f, 0.f, s.z, 0.f,
        0.f, 0.f, 0.f, 1.f);
}

template<typename T>
requires is_scalar_expr_v<T>
[[nodiscard]] constexpr matrix4_t<T, 4> scale(const T &x, const T &y, const T &z) noexcept { return scale(make_float3(x, y, z)); }

template<typename T>
requires is_scalar_expr_v<T>
[[nodiscard]] constexpr matrix4_t<T, 4> scale(const T &s) noexcept { return scale(make_float3(s)); }

template<EPort p = D>
[[nodiscard]] inline oc_float4x4<p> perspective(oc_float<p> fov_y, const oc_float<p> &z_near,
                                                const oc_float<p> &z_far, oc_bool<p> radian = false) {
    fov_y = ocarina::select(radian, fov_y, radians(fov_y));
    oc_float<p> inv_tan = 1 / tan(fov_y / 2.f);
    oc_float4x4<p> mat = make_float4x4(
        inv_tan, 0, 0, 0,
        0, inv_tan, 0, 0,
        0, 0, z_far / (z_far - z_near), 1,
        0, 0, -z_far * z_near / (z_far - z_near), 0);
    return mat;
}

template<EPort p = D>
[[nodiscard]] oc_float4x4<p> rotation(const oc_float3<p> &axis, oc_float<p> angle,
                                      oc_bool<p> radian = false) noexcept {
    angle = ocarina::select(radian, angle, radians(angle));
    oc_float<p> c = cos(angle);
    oc_float<p> s = sin(angle);
    oc_float3<p> a = normalize(axis);
    oc_float3<p> t = (1.f - c) * a;
    return make_float4x4(
        c + t.x * a.x, t.x * a.y + s * a.z, t.x * a.z - s * a.y, 0.0f,
        t.y * a.x - s * a.z, c + t.y * a.y, t.y * a.z + s * a.x, 0.0f,
        t.z * a.x + s * a.y, t.z * a.y - s * a.x, c + t.z * a.z, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
}

template<EPort p = D>
[[nodiscard]] oc_float4x4<p> rotation_x(const oc_float<p> &angle, oc_bool<p> radian = false) noexcept {
    return rotation<p>(make_float3(1, 0, 0), angle, radian);
}

template<EPort p = D>
[[nodiscard]] oc_float4x4<p> rotation_y(const oc_float<p> &angle, oc_bool<p> radian = false) noexcept {
    return rotation<p>(make_float3(0, 1, 0), angle, radian);
}

template<EPort p = D>
[[nodiscard]] oc_float4x4<p> rotation_z(const oc_float<p> &angle, oc_bool<p> radian = false) noexcept {
    return rotation<p>(make_float3(0, 0, 1), angle, radian);
}

template<EPort p = D>
[[nodiscard]] oc_float4x4<p> TRS(const oc_float3<p> &t, const oc_float4<p> &r,
                                 const oc_float3<p> &s) noexcept {
    oc_float4x4<p> T = translation(t);
    oc_float4x4<p> R = rotation<p>(make_float3(r), r.w, false);
    oc_float4x4<p> S = scale(s);
    return T * R * S;
}

template<EPort p = D>
[[nodiscard]] oc_float4x4<p> TRS(const oc_float3<p> &t, const oc_quaternion<p> &r,
                                 const oc_float3<p> &s) noexcept {
    oc_float<p> x = r.v().x;
    oc_float<p> y = r.v().y;
    oc_float<p> z = r.v().z;
    oc_float<p> w = r.w();
    oc_float<p> xx = x * x;
    oc_float<p> yy = y * y;
    oc_float<p> zz = z * z;
    oc_float<p> xy = x * y;
    oc_float<p> xz = x * z;
    oc_float<p> yz = y * z;
    oc_float<p> xw = x * w;
    oc_float<p> yw = y * w;
    oc_float<p> zw = z * w;

    oc_float4x4<p> R = make_float4x4(
        1.f - 2.f * (yy + zz), 2.f * (xy + zw), 2.f * (xz - yw), 0.f,
        2.f * (xy - zw), 1.f - 2.f * (xx + zz), 2.f * (yz + xw), 0.f,
        2.f * (xz + yw), 2.f * (yz - xw), 1.f - 2.f * (xx + yy), 0.f,
        0.f, 0.f, 0.f, 1.f);

    oc_float4x4<p> T = translation(t);
    oc_float4x4<p> S = scale(s);
    return T * R * S;
}

template<EPort p = D>
[[nodiscard]] oc_float4x4<p> look_at(const oc_float3<p> &eye, const oc_float3<p> &target_pos,
                                     oc_float3<p> up) noexcept {
    oc_float3<p> fwd = normalize(target_pos - eye);
    oc_float3<p> right = normalize(cross(fwd, up));
    up = normalize(cross(right, fwd));
    return make_float4x4(
        right.x, right.y, right.z, 0.f,
        up.x, up.y, up.z, 0.f,
        fwd.x, fwd.y, fwd.z, 0.f,
        eye.x, eye.y, eye.z, 1.f);
}

template<EPort p = D>
[[nodiscard]] oc_float3<p> transform_vector(const oc_float4x4<p> &m, const oc_float3<p> &vec) noexcept {
    oc_float3x3<p> mat3x3 = make_float3x3(m);
    return mat3x3 * vec;
}

template<EPort p = D>
[[nodiscard]] oc_float3<p> transform_point(const oc_float4x4<p> &m, const oc_float3<p> &point) noexcept {
    oc_float4<p> homo_point = make_float4(point, 1.f);
    return make_float3(m * homo_point);
}

template<EPort p = D>
[[nodiscard]] oc_float3<p> transform_normal(const oc_float4x4<p> &m, const oc_float3<p> &normal) noexcept {
    return make_float3(transpose(inverse(m)) * make_float4(normal, 0.f));
}

template<EPort p = D>
[[nodiscard]] var_t<Ray, p> transform_ray(const oc_float4x4<p> &m, var_t<Ray, p> ray) noexcept {
    ray.org_min = make_float4(transform_point(m, ray.org_min.xyz()), ray.org_min.w);
    ray.dir_max = make_float4(transform_vector(m, ray.dir_max.xyz()), ray.dir_max.w);
    return ray;
}

inline void decompose(float4x4 m, float3 *T, quaternion *quat, float3 *s) noexcept {
    T->x = m[3][0];
    T->y = m[3][1];
    T->z = m[3][2];
    float4x4 M = m;

    for (int i = 0; i < 3; ++i) {
        M[i][3] = M[3][i] = 0.f;
    }
    M[3][3] = 1.f;

    float norm = 1;
    int count = 0;
    float4x4 R = M;
    do {
        float4x4 Rnext;
        float4x4 Rit = inverse(transpose(R));
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                Rnext[i][j] = 0.5f * (R[i][j] + Rit[i][j]);

        norm = 0;
        for (int i = 0; i < 3; ++i) {
            float n = std::abs(R[i][0] - Rnext[i][0]) +
                      std::abs(R[i][1] - Rnext[i][1]) +
                      std::abs(R[i][2] - Rnext[i][2]);
            norm = std::max(norm, n);
        }
        R = Rnext;
    } while (++count < 100 && norm > 0.0001f);
    *quat = quaternion ::from_float4x4(R);
    float4x4 S = inverse(R) * M;
    *s = make_float3(S[0][0], S[1][1], S[2][2]);
}

[[nodiscard]] inline Box3f transform_box(float4x4 mat, const Box3f &b) noexcept {
    float3 minPoint = make_float3(mat[3][0], mat[3][1], mat[3][2]);
    float3 maxPoint = minPoint;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            float e = mat[j][i];
            float p1 = e * b.lower[j];
            float p2 = e * b.upper[j];
            if (p1 > p2) {
                minPoint[i] += p2;
                maxPoint[i] += p1;
            } else {
                minPoint[i] += p1;
                maxPoint[i] += p2;
            }
        }
    }
    return {minPoint, maxPoint};
}

}// namespace transform

template<typename TMat>
requires is_matrix4_v<expr_value_t<TMat>>
struct Transform {
private:
    TMat _mat;
    static constexpr auto port = port_v<TMat>;

    [[nodiscard]] static oc_float3<port> extract_translation(const TMat &mat) noexcept {
        return make_float3(mat[3][0], mat[3][1], mat[3][2]);
    }

    [[nodiscard]] static oc_float3<port> extract_scale(const TMat &mat) noexcept {
        oc_float3<port> x = make_float3(mat[0][0], mat[0][1], mat[0][2]);
        oc_float3<port> y = make_float3(mat[1][0], mat[1][1], mat[1][2]);
        oc_float3<port> z = make_float3(mat[2][0], mat[2][1], mat[2][2]);
        return make_float3(length(x), length(y), length(z));
    }

public:
    Transform() noexcept : _mat(1) {}
    explicit Transform(const TMat &mat) : _mat(mat) {}
    [[nodiscard]] TMat mat4x4() const noexcept { return _mat; }
    [[nodiscard]] TMat inv_mat4x4() const noexcept { return inverse(_mat); }
    [[nodiscard]] oc_float3x3<port> mat3x3() const noexcept { return make_float3x3(_mat); }
    [[nodiscard]] oc_float3x3<port> inv_mat3x3() const noexcept { return inverse(mat3x3()); }
    template<typename M>
    [[nodiscard]] auto operator*(const Transform<M> &other) const noexcept { return Transform(_mat * other._mat); }
    template<typename T>
    [[nodiscard]] auto apply_vector(const T &vec) noexcept { return transform_vector<port_v<TMat, T>>(_mat, vec); }
    template<typename T>
    [[nodiscard]] auto apply_point(const T &point) noexcept { return transform_point<port_v<TMat, T>>(_mat, point); }
    template<typename T>
    [[nodiscard]] auto apply_normal(const T &normal) noexcept { return transform_normal<port_v<TMat, T>>(_mat, normal); }
    template<typename T>
    [[nodiscard]] auto apply_ray(T &&ray) noexcept { return transform_ray<port_v<TMat, T>>(_mat, OC_FORWARD(ray)); }

    [[nodiscard]] auto position() const noexcept {
        return extract_translation(_mat);
    }

    [[nodiscard]] auto scale() const noexcept {
        oc_float3<port> x = make_float3(_mat[0][0], _mat[0][1], _mat[0][2]);
        oc_float3<port> y = make_float3(_mat[1][0], _mat[1][1], _mat[1][2]);
        oc_float3<port> z = make_float3(_mat[2][0], _mat[2][1], _mat[2][2]);
        return make_float3(length(x), length(y), length(z));
    }

    [[nodiscard]] oc_float4<port> rotation() const noexcept {
        oc_float3<port> s = scale();
        TMat R = _mat;
        if (s.x != 0.f) {
            R[0][0] /= s.x;
            R[0][1] /= s.x;
            R[0][2] /= s.x;
        }
        if (s.y != 0.f) {
            R[1][0] /= s.y;
            R[1][1] /= s.y;
            R[1][2] /= s.y;
        }
        if (s.z != 0.f) {
            R[2][0] /= s.z;
            R[2][1] /= s.z;
            R[2][2] /= s.z;
        }
        auto quat = oc_quaternion<port>::from_float4x4(R);
        return make_float4(quat.v(), quat.w());
    }

    void set_position(const oc_float3<port>& position) noexcept {
        _mat[3][0] = position.x;
        _mat[3][1] = position.y;
        _mat[3][2] = position.z;
    }

    void set_rotation(const oc_quaternion<port>& rotation) noexcept {
        _mat = TRS<port>(position(), rotation, scale());
    }

    void set_scale(const oc_float3<port>& scale) noexcept {
        _mat = TRS<port>(position(), rotation(), scale);
    }

    void set_TRS(const oc_float3<port> &translation, const oc_quaternion<port> &rotation, const oc_float3<port> &scale) noexcept {
        _mat = TRS<port>(translation, rotation, scale);
    }
};

}// namespace ocarina
//
// CPU-side transform utilities for the framework (no DSL dependency).
//

#pragma once

#include "math/basic_types.h"
#include <cmath>

namespace ocarina {

#include "math/common_lib.inl.h"

struct quaternion {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float w = 1.f;

    quaternion() = default;
    quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    [[nodiscard]] float3 v() const noexcept { return make_float3(x, y, z); }

    static quaternion from_float3x3(float3x3 m) {
        float3 v;
        float w_val;
        float trace = m[0][0] + m[1][1] + m[2][2];
        if (trace > 0.f) {
            float s = std::sqrt(trace + 1.0f);
            w_val = s / 2.0f;
            s = 0.5f / s;
            v.x = (m[1][2] - m[2][1]) * s;
            v.y = (m[2][0] - m[0][2]) * s;
            v.z = (m[0][1] - m[1][0]) * s;
        } else {
            const int nxt[3] = {1, 2, 0};
            float q[3];
            int i = 0;
            if (m[1][1] > m[0][0]) i = 1;
            if (m[2][2] > m[i][i]) i = 2;
            int j = nxt[i];
            int k = nxt[j];
            float s = std::sqrt((m[i][i] - (m[j][j] + m[k][k])) + 1.0f);
            q[i] = s * 0.5f;
            if (s != 0.f) s = 0.5f / s;
            w_val = (m[j][k] - m[k][j]) * s;
            q[j] = (m[i][j] + m[j][i]) * s;
            q[k] = (m[i][k] + m[k][i]) * s;
            v.x = q[0];
            v.y = q[1];
            v.z = q[2];
        }
        return quaternion(v.x, v.y, v.z, w_val);
    }

    static quaternion from_float4x4(float4x4 m) {
        return from_float3x3(make_float3x3(m));
    }
};

[[nodiscard]] inline float4x4 translation_matrix(const float3 &t) noexcept {
    return make_float4x4(
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        t.x, t.y, t.z, 1.f);
}

[[nodiscard]] inline float4x4 scale_matrix(const float3 &s) noexcept {
    return make_float4x4(
        s.x, 0.f, 0.f, 0.f,
        0.f, s.y, 0.f, 0.f,
        0.f, 0.f, s.z, 0.f,
        0.f, 0.f, 0.f, 1.f);
}

[[nodiscard]] inline float4x4 rotation_matrix(const quaternion &r) noexcept {
    float x = r.x, y = r.y, z = r.z, w = r.w;
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;
    float xw = x * w, yw = y * w, zw = z * w;
    return make_float4x4(
        1.f - 2.f * (yy + zz), 2.f * (xy + zw), 2.f * (xz - yw), 0.f,
        2.f * (xy - zw), 1.f - 2.f * (xx + zz), 2.f * (yz + xw), 0.f,
        2.f * (xz + yw), 2.f * (yz - xw), 1.f - 2.f * (xx + yy), 0.f,
        0.f, 0.f, 0.f, 1.f);
}

[[nodiscard]] inline float4x4 TRS(const float3 &t, const quaternion &r, const float3 &s) noexcept {
    return translation_matrix(t) * rotation_matrix(r) * scale_matrix(s);
}

template<typename TMat>
struct Transform {
    TMat _mat{1};

    Transform() noexcept = default;
    explicit Transform(const TMat &mat) : _mat(mat) {}

    [[nodiscard]] TMat mat4x4() const noexcept { return _mat; }

    [[nodiscard]] float3 position() const noexcept {
        return make_float3(_mat[3][0], _mat[3][1], _mat[3][2]);
    }

    [[nodiscard]] float3 scale() const noexcept {
        float3 x = make_float3(_mat[0][0], _mat[0][1], _mat[0][2]);
        float3 y = make_float3(_mat[1][0], _mat[1][1], _mat[1][2]);
        float3 z = make_float3(_mat[2][0], _mat[2][1], _mat[2][2]);
        return make_float3(length(x), length(y), length(z));
    }

    void set_position(const float3 &position) noexcept {
        _mat[3][0] = position.x;
        _mat[3][1] = position.y;
        _mat[3][2] = position.z;
    }

    void set_TRS(const float3 &translation, const quaternion &rotation, const float3 &sc) noexcept {
        _mat = TRS(translation, rotation, sc);
    }
};

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
    *quat = quaternion::from_float4x4(R);
    float4x4 S = inverse(R) * M;
    *s = make_float3(S[0][0], S[1][1], S[2][2]);
}

}// namespace ocarina

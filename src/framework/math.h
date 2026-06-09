//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/stl.h"
#include <cmath>

namespace ocarina {

// Left-handed coordinate system: +X right, +Y up, +Z forward (DirectX-style).
namespace math3d {
using Vector2D = std::array<float, 2>;
using Vector3D = std::array<float, 3>;
using Vector4D = std::array<float, 4>;

inline Vector3D operator-(const Vector3D& l, const Vector3D& r)
{
    return {l[0] - r[0], l[1] - r[1], l[2] - r[2]};
}

inline Vector3D operator+(const Vector3D &l, const Vector3D &r) {
    return {l[0] + r[0], l[1] + r[1], l[2] + r[2]};
}

// Normalize
inline Vector3D normalize(const Vector3D &v) {
    float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return len > 0.0f ? Vector3D{v[0] / len, v[1] / len, v[2] / len} : Vector3D({v[0] / len, v[1] / len, v[2] / len});
}

// Cross product (left-handed: a x b follows the left-hand rule).
inline Vector3D cross(const Vector3D &a, const Vector3D &b) {
    return {a[2] * b[1] - a[1] * b[2],
            a[0] * b[2] - a[2] * b[0],
            a[1] * b[0] - a[0] * b[1]};
}

// Dot product
inline float dot(const Vector3D &a, const Vector3D &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

inline float rad_to_deg(float rad) {
    return rad * (180.0f / static_cast<float>(M_PI));
}

inline float deg_to_rad(float deg) {
    return deg * (static_cast<float>(M_PI) / 180.0f);
}

// Double versions
inline double rad_to_deg(double rad) {
    return rad * (180.0 / M_PI);
}

inline double deg_to_rad(double deg) {
    return deg * (M_PI / 180.0);
}

struct Matrix4 {
    std::array<float, 16> m;
    void set_identity() {
        m = {1, 0, 0, 0,
             0, 1, 0, 0,
             0, 0, 1, 0,
             0, 0, 0, 1};
    }
    Matrix4() { set_identity(); }

    Matrix4(float m11, float m12, float m13, float m14,
        float m21, float m22, float m23, float m24,
        float m31, float m32, float m33, float m34,
        float m41, float m42, float m43, float m44)
    {
        m = {m11, m12, m13, m14,
             m21, m22, m23, m24,
             m31, m32, m33, m34,
             m41, m42, m43, m44
        };
    }

    Matrix4(const Vector4D& col1, const Vector4D& col2, const Vector4D& col3, const Vector4D& col4)
    {
        m[0] = col1[0];
        m[4] = col1[1];
        m[8] = col1[2];
        m[12] = col1[3];
    }

    static Matrix4 identity() {
        Matrix4 mat;
        mat.set_identity();
        return mat;
    }

    // Access element (row, col)
    float &operator()(int row, int col) { return m[row * 4 + col]; }
    float operator()(int row, int col) const { return m[row * 4 + col]; }

    // Column access
    float *col(int c) { return &m[c * 4]; }
    const float *col(int c) const { return &m[c * 4]; }

    // Matrix multiplication
    Matrix4 operator*(const Matrix4 &rhs) const {
        Matrix4 r;
        for (int c = 0; c < 4; ++c) {
            for (int rRow = 0; rRow < 4; ++rRow) {
                r(rRow, c) = 0.0f;
                for (int k = 0; k < 4; ++k)
                    r(rRow, c) += (*this)(rRow, k) * rhs(k, c);
            }
        }
        return r;
    }

    // Matrix-vector multiply (vec4, column-major convention)
    Vector4D multiply_vector4(const Vector4D &v) const {
        // Column-major storage with column-vector convention: r = M * v
        // r = v0*C0 + v1*C1 + v2*C2 + v3*C3
        Vector4D r{};
        r[0] = m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12] * v[3];
        r[1] = m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13] * v[3];
        r[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14] * v[3];
        r[3] = m[3] * v[0] + m[7] * v[1] + m[11] * v[2] + m[15] * v[3];
        return r;
    };

    // Matrixvector multiply (vec3, assume w=1)
    Vector3D multiplyVec3(const Vector3D &v) const {
        std::array<float, 3> r{};
        r[0] = m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12];
        r[1] = m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13];
        r[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14];
        return r;
    }

    // Transpose
    Matrix4 transpose() const {
        Matrix4 r;
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                r(row, col) = (*this)(col, row);
        return r;
    }

    // Determinant (Laplace expansion, simplified for 4x4)
    float determinant() const {
        const float *a = m.data();
        float det =
            a[0] * (a[5] * (a[10] * a[15] - a[14] * a[11]) - a[9] * (a[6] * a[15] - a[14] * a[7]) + a[13] * (a[6] * a[11] - a[10] * a[7])) - a[4] * (a[1] * (a[10] * a[15] - a[14] * a[11]) - a[9] * (a[2] * a[15] - a[14] * a[3]) + a[13] * (a[2] * a[11] - a[10] * a[3])) + a[8] * (a[1] * (a[6] * a[15] - a[14] * a[7]) - a[5] * (a[2] * a[15] - a[14] * a[3]) + a[13] * (a[2] * a[7] - a[6] * a[3])) - a[12] * (a[1] * (a[6] * a[11] - a[10] * a[7]) - a[5] * (a[2] * a[11] - a[10] * a[3]) + a[9] * (a[2] * a[7] - a[6] * a[3]));
        return det;
    }

    // Inverse (using adjugate / determinant)
    Matrix4 inverse() const {
        Matrix4 inv;
        const float *a = m.data();
        float *invOut = inv.m.data();

        invOut[0] = a[5] * (a[10] * a[15] - a[14] * a[11]) - a[9] * (a[6] * a[15] - a[14] * a[7]) + a[13] * (a[6] * a[11] - a[10] * a[7]);
        invOut[4] = -(a[4] * (a[10] * a[15] - a[14] * a[11]) - a[8] * (a[6] * a[15] - a[14] * a[7]) + a[12] * (a[6] * a[11] - a[10] * a[7]));
        invOut[8] = a[4] * (a[9] * a[15] - a[13] * a[11]) - a[8] * (a[5] * a[15] - a[13] * a[7]) + a[12] * (a[5] * a[11] - a[9] * a[7]);
        invOut[12] = -(a[4] * (a[9] * a[14] - a[13] * a[10]) - a[8] * (a[5] * a[14] - a[13] * a[6]) + a[12] * (a[5] * a[10] - a[9] * a[6]));

        invOut[1] = -(a[1] * (a[10] * a[15] - a[14] * a[11]) - a[9] * (a[2] * a[15] - a[14] * a[3]) + a[13] * (a[2] * a[11] - a[10] * a[3]));
        invOut[5] = a[0] * (a[10] * a[15] - a[14] * a[11]) - a[8] * (a[2] * a[15] - a[14] * a[3]) + a[12] * (a[2] * a[11] - a[10] * a[3]);
        invOut[9] = -(a[0] * (a[9] * a[15] - a[13] * a[11]) - a[8] * (a[1] * a[15] - a[13] * a[3]) + a[12] * (a[1] * a[11] - a[9] * a[3]));
        invOut[13] = a[0] * (a[9] * a[14] - a[13] * a[10]) - a[8] * (a[1] * a[14] - a[13] * a[2]) + a[12] * (a[1] * a[10] - a[9] * a[2]);

        invOut[2] = a[1] * (a[6] * a[15] - a[14] * a[7]) - a[5] * (a[2] * a[15] - a[14] * a[3]) + a[13] * (a[2] * a[7] - a[6] * a[3]);
        invOut[6] = -(a[0] * (a[6] * a[15] - a[14] * a[7]) - a[4] * (a[2] * a[15] - a[14] * a[3]) + a[12] * (a[2] * a[7] - a[6] * a[3]));
        invOut[10] = a[0] * (a[5] * a[15] - a[13] * a[7]) - a[4] * (a[1] * a[15] - a[13] * a[3]) + a[12] * (a[1] * a[7] - a[5] * a[3]);
        invOut[14] = -(a[0] * (a[5] * a[14] - a[13] * a[6]) - a[4] * (a[1] * a[14] - a[13] * a[2]) + a[12] * (a[1] * a[6] - a[5] * a[2]));

        invOut[3] = -(a[1] * (a[6] * a[11] - a[10] * a[7]) - a[5] * (a[2] * a[11] - a[10] * a[3]) + a[9] * (a[2] * a[7] - a[6] * a[3]));
        invOut[7] = a[0] * (a[6] * a[11] - a[10] * a[7]) - a[4] * (a[2] * a[11] - a[10] * a[3]) + a[8] * (a[2] * a[7] - a[6] * a[3]);
        invOut[11] = -(a[0] * (a[5] * a[11] - a[9] * a[7]) - a[4] * (a[1] * a[11] - a[9] * a[3]) + a[8] * (a[1] * a[7] - a[5] * a[3]));
        invOut[15] = a[0] * (a[5] * a[10] - a[9] * a[6]) - a[4] * (a[1] * a[10] - a[9] * a[2]) + a[8] * (a[1] * a[6] - a[5] * a[2]);

        float det = a[0] * invOut[0] + a[1] * invOut[4] + a[2] * invOut[8] + a[3] * invOut[12];
        if (fabs(det) < 1e-8f)
            throw std::runtime_error("Matrix is singular and cannot be inverted.");

        float invDet = 1.0f / det;
        for (int i = 0; i < 16; ++i)
            invOut[i] *= invDet;

        return inv;
    }

    // --- Common Transformation Functions ---

    static Matrix4 translate(float x, float y, float z) {
        Matrix4 mat = Matrix4::identity();
        mat(0, 3) = x;
        mat(1, 3) = y;
        mat(2, 3) = z;
        return mat;
    }

    static Matrix4 scale(float x, float y, float z) {
        Matrix4 mat;
        mat.set_identity();
        mat(0, 0) = x;
        mat(1, 1) = y;
        mat(2, 2) = z;
        return mat;
    }

    static Matrix4 rotateX(float radians) {
        Matrix4 mat = Matrix4::identity();
        float c = cosf(radians);
        float s = sinf(radians);
        mat(1, 1) = c;
        mat(2, 1) = s;
        mat(1, 2) = -s;
        mat(2, 2) = c;
        return mat;
    }

    static Matrix4 rotateY(float radians) {
        Matrix4 mat = Matrix4::identity();
        float c = cosf(radians);
        float s = sinf(radians);
        mat(0, 0) = c;
        mat(2, 0) = -s;
        mat(0, 2) = s;
        mat(2, 2) = c;
        return mat;
    }

    static Matrix4 rotateZ(float radians) {
        Matrix4 mat = Matrix4::identity();
        float c = cosf(radians);
        float s = sinf(radians);
        mat(0, 0) = c;
        mat(1, 0) = s;
        mat(0, 1) = -s;
        mat(1, 1) = c;
        return mat;
    }

    static Matrix4 perspective(float fovYRadians, float aspect, float zNear, float zFar) {
        Matrix4 mat;
        for (auto &v : mat.m) v = 0.0f;

        float f = 1.0f / tanf(fovYRadians * 0.5f);
        mat(0, 0) = f / aspect;
        mat(1, 1) = f;
        mat(2, 2) = zFar / (zFar - zNear);
        mat(2, 3) = -(zFar * zNear) / (zFar - zNear);
        mat(3, 2) = 1.0f;
        return mat;
    }

    static Matrix4 ortho(float left, float right, float bottom, float top, float zNear, float zFar) {
        Matrix4 mat;
        mat.set_identity();
        mat(0, 0) = 2.0f / (right - left);
        mat(1, 1) = 2.0f / (top - bottom);
        mat(2, 2) = 1.0f / (zFar - zNear);
        mat(0, 3) = -(right + left) / (right - left);
        mat(1, 3) = -(top + bottom) / (top - bottom);
        mat(2, 3) = -zNear / (zFar - zNear);
        return mat;
    }

    static Matrix4 look_at(const Vector3D& eye, const Vector3D& target, const Vector3D& up)
    {
        Vector3D f = normalize(target - eye);
        Vector3D r = normalize(cross(up, f));
        Vector3D u = cross(f, r);

        Matrix4 result(
            r[0], r[1], r[2], -dot(r, eye),
            u[0], u[1], u[2], -dot(u, eye),
            f[0], f[1], f[2], -dot(f, eye),
            0, 0, 0, 1.0f);
        return result;
    }
};

}


}// namespace ocarina
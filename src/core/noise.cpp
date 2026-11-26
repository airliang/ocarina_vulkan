#include "noise.h"

#include <cmath>

namespace ocarina {

namespace {

// Ken Perlin's improved noise permutation table (repeated to 512 for wrap).
constexpr uint8_t kPermutation[256] = {
    151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225,
    140, 36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10, 23, 190, 6, 148,
    247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32,
    57, 177, 33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175,
    74, 165, 71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122,
    60, 211, 133, 230, 220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54,
    65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169,
    200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64,
    52, 217, 226, 250, 124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212,
    207, 206, 59, 227, 47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213,
    119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9,
    129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104,
    218, 246, 97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241,
    81, 51, 145, 235, 249, 14, 239, 107, 49, 192, 214, 31, 181, 199, 106, 157,
    184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93,
    222, 114, 67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180};

[[nodiscard]] inline int perm(int i) noexcept {
    return static_cast<int>(kPermutation[i & 255]);
}

[[nodiscard]] inline float fade(float t) noexcept {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

[[nodiscard]] inline float lerp(float t, float a, float b) noexcept {
    return a + t * (b - a);
}

[[nodiscard]] inline float grad2(int hash, float x, float y) noexcept {
    const int h = hash & 7;
    const float u = h < 4 ? x : y;
    const float v = h < 4 ? y : x;
    return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -2.0f * v : 2.0f * v);
}

[[nodiscard]] inline float grad3(int hash, float x, float y, float z) noexcept {
    const int h = hash & 15;
    const float u = h < 8 ? x : y;
    const float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -v : v);
}

}// namespace

float perlin_noise_2d(float x, float y) noexcept {
    const int xi = static_cast<int>(std::floor(x)) & 255;
    const int yi = static_cast<int>(std::floor(y)) & 255;
    const float xf = x - std::floor(x);
    const float yf = y - std::floor(y);
    const float u = fade(xf);
    const float v = fade(yf);

    const int aa = perm(perm(xi) + yi);
    const int ab = perm(perm(xi) + yi + 1);
    const int ba = perm(perm(xi + 1) + yi);
    const int bb = perm(perm(xi + 1) + yi + 1);

    const float x1 = lerp(u, grad2(aa, xf, yf), grad2(ba, xf - 1.0f, yf));
    const float x2 = lerp(u, grad2(ab, xf, yf - 1.0f), grad2(bb, xf - 1.0f, yf - 1.0f));
    return lerp(v, x1, x2);
}

float perlin_noise_2d(float2 p) noexcept {
    return perlin_noise_2d(p.x, p.y);
}

float perlin_noise_3d(float x, float y, float z) noexcept {
    const int xi = static_cast<int>(std::floor(x)) & 255;
    const int yi = static_cast<int>(std::floor(y)) & 255;
    const int zi = static_cast<int>(std::floor(z)) & 255;
    const float xf = x - std::floor(x);
    const float yf = y - std::floor(y);
    const float zf = z - std::floor(z);
    const float u = fade(xf);
    const float v = fade(yf);
    const float w = fade(zf);

    const int a = perm(xi) + yi;
    const int aa = perm(a) + zi;
    const int ab = perm(a + 1) + zi;
    const int b = perm(xi + 1) + yi;
    const int ba = perm(b) + zi;
    const int bb = perm(b + 1) + zi;

    const float x1 = lerp(u, grad3(perm(aa), xf, yf, zf), grad3(perm(ba), xf - 1.0f, yf, zf));
    const float x2 = lerp(
        u,
        grad3(perm(ab), xf, yf - 1.0f, zf),
        grad3(perm(bb), xf - 1.0f, yf - 1.0f, zf));
    const float y1 = lerp(v, x1, x2);

    const float x3 = lerp(
        u,
        grad3(perm(aa + 1), xf, yf, zf - 1.0f),
        grad3(perm(ba + 1), xf - 1.0f, yf, zf - 1.0f));
    const float x4 = lerp(
        u,
        grad3(perm(ab + 1), xf, yf - 1.0f, zf - 1.0f),
        grad3(perm(bb + 1), xf - 1.0f, yf - 1.0f, zf - 1.0f));
    const float y2 = lerp(v, x3, x4);

    return lerp(w, y1, y2);
}

float perlin_noise_3d(float3 p) noexcept {
    return perlin_noise_3d(p.x, p.y, p.z);
}

float perlin_fbm_2d(float2 p, uint32_t octaves, float lacunarity, float gain) noexcept {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float max_amp = 0.0f;
    for (uint32_t i = 0; i < octaves; ++i) {
        sum += amplitude * perlin_noise_2d(p * frequency);
        max_amp += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    return max_amp > 0.0f ? sum / max_amp : 0.0f;
}

float perlin_fbm_3d(float3 p, uint32_t octaves, float lacunarity, float gain) noexcept {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float max_amp = 0.0f;
    for (uint32_t i = 0; i < octaves; ++i) {
        sum += amplitude * perlin_noise_3d(p * frequency);
        max_amp += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    return max_amp > 0.0f ? sum / max_amp : 0.0f;
}

}// namespace ocarina

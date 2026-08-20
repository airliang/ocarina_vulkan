#include "descriptor_bindings.hlsl"

struct Transform
{
    float4x4 modelMatrix;
    float4x4 modelMatrixInverse;
};

[[vk::binding(0, SCENE_SET)]] StructuredBuffer<Transform> transforms : register(t0);

Transform LoadTransform(uint transformIndex)
{
    return transforms[transformIndex];
}

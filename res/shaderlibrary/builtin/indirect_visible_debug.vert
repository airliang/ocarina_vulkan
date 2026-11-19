#include "frame.hlsl"

// Absolute world positions. This pass pushes its own view-projection so it can
// frame the whole instance grid without changing the main camera uniform buffer.
struct VisibleDebugConstants
{
    float4x4 viewProj;
};

[[vk::push_constant]]
VisibleDebugConstants debugConstants;

[[vk::binding(1, MATERIAL_SET)]] StructuredBuffer<uint> visibleIndices : register(t2);

struct VSInput
{
    [[vk::location(0)]] float3 Pos : POSITION0;
    [[vk::location(1)]] float3 Normal : NORMAL0;
    [[vk::location(2)]] float2 UV : TEXCOORD0;
    [[vk::location(3)]] float4 Color : COLOR0;
    [[vk::location(4)]] float4 Tangent : TANGENT0;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
};

VSOutput main(VSInput input, uint instanceId : SV_InstanceID)
{
    VSOutput output;
    Transform transform = LoadTransform(visibleIndices[instanceId]);
    float4 worldPos = mul(transform.modelMatrix, float4(input.Pos, 1.0));
    output.Pos = mul(debugConstants.viewProj, worldPos);
    return output;
}

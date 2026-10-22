#include "frame.hlsl"

[[vk::binding(0, MATERIAL_SET)]] TextureCube skybox : register(t0);
[[vk::binding(1, MATERIAL_SET)]] SamplerState sampler_skybox : register(s0);

struct VSOutput
{
    [[vk::location(0)]] float2 ClipXY : TEXCOORD0;
};

float4 main(VSOutput input) : SV_TARGET
{
    // Ray from the camera through the far clip plane so the skybox follows camera position.
    float4 world_pos = mul(invViewProjMatrix, float4(input.ClipXY, 1.0, 1.0));
    float3 world = world_pos.xyz / max(world_pos.w, 1e-6);
    float3 direction = normalize(world - GetCameraPositionWS());
    float3 color = skybox.SampleLevel(sampler_skybox, direction, 0.0).rgb;
    return float4(color, 1.0);
}

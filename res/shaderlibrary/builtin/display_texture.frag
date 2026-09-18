// Sample a MATERIAL_SET texture with UVs reconstructed from fullscreen.vert ClipXY.
#include "descriptor_bindings.hlsl"

[[vk::binding(0, MATERIAL_SET)]] Texture2D albedo : register(t1);
[[vk::binding(1, MATERIAL_SET)]] SamplerState sampler_albedo : register(s1);

struct VSOutput
{
    [[vk::location(0)]] float2 ClipXY : TEXCOORD0;
};

float4 main(VSOutput input) : SV_TARGET
{
    float2 uv = input.ClipXY * float2(0.5, -0.5) + 0.5;
    float4 color = albedo.Sample(sampler_albedo, uv);
    return float4(color.rgb, 1.0);
}

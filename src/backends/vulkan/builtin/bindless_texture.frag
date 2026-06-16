// Copyright 2020 Google LLC
#include "common.hlsl"
#include "push_constant.hlsl"

// No PER_OBJECT_SET in this shader: bindless is set 2 (GLOBAL=0, MATERIAL=1).
[[vk::binding(0, MATERIAL_SET)]]Texture2D g_textures[];
[[vk::binding(1, MATERIAL_SET)]]SamplerState samplers[];

[[vk::push_constant]]
PushConstants pushConstants;

struct VSOutput
{
[[vk::location(0)]] float2 UV : TEXCOORD0;
[[vk::location(1)]] float4 Color : COLOR0;
};

float4 main(VSOutput input) : SV_TARGET
{
	float4 color = g_textures[pushConstants.albedoIndex].Sample(samplers[pushConstants.albedoSamplerIndex], input.UV);

	return float4(color.rgb, 1.0);
}
// Copyright 2020 Google LLC

#include "common.hlsl"
#include "push_constant.hlsl"

struct VSInput
{
[[vk::location(0)]] float3 Pos : POSITION0;
[[vk::location(1)]] float3 Normal : NORMAL0;
[[vk::location(2)]] float2 UV : TEXCOORD0;
[[vk::location(3)]] float4 Color : COLOR0;
};

[[vk::push_constant]]
PushConstants pushConstants;

struct VSOutput
{
	float4 Pos : SV_POSITION;
[[vk::location(0)]] float3 Normal : NORMAL0;
[[vk::location(1)]] float3 Color : COLOR0;
[[vk::location(2)]] float2 UV : TEXCOORD0;
[[vk::location(3)]] float3 ViewVec : TEXCOORD1;
[[vk::location(4)]] float3 LightVec : TEXCOORD2;
};

VSOutput main(VSInput input)
{
	VSOutput output = (VSOutput)0;
	float4 worldPos = mul(pushConstants.modelMatrix, float4(input.Pos, 1.0));
	float4 viewPos = mul(viewMatrix, worldPos);
	output.Pos = mul(projectionMatrix, viewPos);

	float3x3 normalMatrix = transpose((float3x3)pushConstants.modelMatrixInverse);
	output.Normal = normalize(mul(normalMatrix, input.Normal));
	output.Color = input.Color.rgb;
	output.UV = input.UV;
	output.LightVec = lightPos.xyz - worldPos.xyz;
	output.ViewVec = cameraPos.xyz - worldPos.xyz;
	return output;
}

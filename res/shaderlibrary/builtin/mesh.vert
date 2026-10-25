// Copyright 2020 Google LLC

#include "frame.hlsl"
#include "push_constant.hlsl"

#ifndef INDIRECT_INSTANCE
#define INDIRECT_INSTANCE 0
#endif

#if INDIRECT_INSTANCE
// Compacted ECS entity indices. World matrices stay in g_transforms.
[[vk::binding(1, MATERIAL_SET)]] StructuredBuffer<uint> visibleIndices : register(t2);
#endif

struct VSInput
{
[[vk::location(0)]] float3 Pos : POSITION0;
[[vk::location(1)]] float3 Normal : NORMAL0;
[[vk::location(2)]] float2 UV : TEXCOORD0;
[[vk::location(3)]] float4 Color : COLOR0;
[[vk::location(4)]] float4 Tangent : TANGENT0;
};

#if !INDIRECT_INSTANCE
[[vk::push_constant]]
PushConstants pushConstants;
#endif

struct VSOutput
{
	float4 Pos : SV_POSITION;
[[vk::location(0)]] float3 Normal : NORMAL0;
[[vk::location(1)]] float3 Color : COLOR0;
[[vk::location(2)]] float2 UV : TEXCOORD0;
[[vk::location(3)]] float3 ViewVec : TEXCOORD1;
[[vk::location(4)]] float3 LightVec : TEXCOORD2;
[[vk::location(5)]] float3 WorldPos : TEXCOORD3;
[[vk::location(6)]] float4 Tangent : TANGENT0;
};

VSOutput main(VSInput input, uint instanceId : SV_InstanceID)
{
	VSOutput output = (VSOutput)0;
#if INDIRECT_INSTANCE
    Transform transform = LoadTransform(visibleIndices[instanceId]);
#else
    Transform transform = LoadTransform(pushConstants.transform_index);
#endif
	float4x4 modelMatrix = ApplyCameraTranslationToMatrix(transform.modelMatrix);
	float4 worldPos = mul(modelMatrix, float4(input.Pos, 1.0));
	float4 viewPos = mul(viewMatrix, worldPos);
	output.Pos = mul(projectionMatrix, viewPos);

	float3x3 normalMatrix = transpose((float3x3)transform.modelMatrixInverse);
	output.Normal = normalize(mul(normalMatrix, input.Normal));
	float3 worldTangent = normalize(mul(normalMatrix, input.Tangent.xyz));
	// Re-orthogonalize after non-uniform scale, keep glTF tangent.w handedness.
	worldTangent = normalize(worldTangent - output.Normal * dot(output.Normal, worldTangent));
	output.Tangent = float4(worldTangent, input.Tangent.w);
	output.Color = input.Color.rgb;
	output.UV = input.UV;
	output.LightVec = -sunDirection.xyz;
	output.ViewVec = GetCameraPositionWS() - worldPos.xyz;
	output.WorldPos = worldPos.xyz;
	return output;
}

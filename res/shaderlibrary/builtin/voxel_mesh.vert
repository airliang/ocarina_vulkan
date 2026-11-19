// Voxel merged-mesh vertex shader.
// Merged chunk meshes are translation-only: no g_transforms matrix.
// Push constants carry chunk_id and the chunk world origin (derived from that id).
// worldPosition = chunk_origin + localVertexPosition.

#include "frame.hlsl"

struct VSInput
{
[[vk::location(0)]] float3 Pos : POSITION0;
[[vk::location(1)]] float3 Normal : NORMAL0;
[[vk::location(2)]] float2 UV : TEXCOORD0;
[[vk::location(3)]] float4 Color : COLOR0;
[[vk::location(4)]] float4 Tangent : TANGENT0;
};

struct PushConstants
{
    uint chunk_id;
    float chunk_origin_x;
    float chunk_origin_y;
    float chunk_origin_z;
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
[[vk::location(5)]] float3 WorldPos : TEXCOORD3;
[[vk::location(6)]] float4 Tangent : TANGENT0;
};

VSOutput main(VSInput input)
{
	VSOutput output = (VSOutput)0;

	float3 chunkOrigin = float3(
		pushConstants.chunk_origin_x,
		pushConstants.chunk_origin_y,
		pushConstants.chunk_origin_z);

	// chunk_id selects the chunk; its world origin is the translation of the merge.
	float3 worldPosAbs = chunkOrigin + input.Pos;
	float3 worldPos = ApplyCameraTranslationToPosition(worldPosAbs);

	float4 viewPos = mul(viewMatrix, float4(worldPos, 1.0));
	output.Pos = mul(projectionMatrix, viewPos);

	// Translation-only: normals / tangents stay in world space as authored.
	output.Normal = normalize(input.Normal);
	float3 worldTangent = normalize(input.Tangent.xyz);
	worldTangent = normalize(worldTangent - output.Normal * dot(output.Normal, worldTangent));
	output.Tangent = float4(worldTangent, input.Tangent.w);
	output.Color = input.Color.rgb;
	output.UV = input.UV;
	output.LightVec = -sunDirection.xyz;
	output.ViewVec = GetCameraPositionWS() - worldPos;
	output.WorldPos = worldPos;
	return output;
}

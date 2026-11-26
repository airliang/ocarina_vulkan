// Voxel mesh fragment shader.
// Albedo comes only from interpolated vertex color — no texture sampling.

#include "frame.hlsl"

struct VSOutput
{
[[vk::location(0)]] float3 Normal : NORMAL0;
[[vk::location(1)]] float3 Color : COLOR0;
[[vk::location(2)]] float2 UV : TEXCOORD0;
[[vk::location(3)]] float3 ViewVec : TEXCOORD1;
[[vk::location(4)]] float3 LightVec : TEXCOORD2;
[[vk::location(5)]] float3 WorldPos : TEXCOORD3;
[[vk::location(6)]] float4 Tangent : TANGENT0;
};

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * denom * denom, 1e-4);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-4);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float4 main(VSOutput input) : SV_TARGET
{
    float3 albedo = input.Color;
    const float roughness = 1.0;
    const float metallic = 0.0;
    const float ao = 1.0;

    float3 N = normalize(input.Normal);
    float3 V = normalize(input.ViewVec);
    float3 L = normalize(input.LightVec);
    float3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(VdotH, F0);

    float3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 1e-4;
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);
    float3 diffuse = kD * albedo / 3.14159265;

    float3 ambient = (albedo * (1.0 - metallic) + F0 * 0.25) * 0.4 * ao;
    // Keep a real read of global_ubo so DXC does not DCE the cbuffer.
    float3 radiance = sunColor.rgb * sunIntensity;
    float3 color = ambient + (diffuse + specular) * radiance * NdotL;
    return float4(color, 1.0);
}

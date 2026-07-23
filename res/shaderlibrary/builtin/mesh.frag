// Copyright 2020 Google LLC
#include "common.hlsl"
#include "material.hlsl"

[[vk::binding(0, MATERIAL_SET)]]Texture2D g_textures[];
[[vk::binding(1, MATERIAL_SET)]]SamplerState samplers[];

struct VSOutput
{
[[vk::location(0)]] float3 Normal : NORMAL0;
[[vk::location(1)]] float3 Color : COLOR0;
[[vk::location(2)]] float2 UV : TEXCOORD0;
[[vk::location(3)]] float3 ViewVec : TEXCOORD1;
[[vk::location(4)]] float3 LightVec : TEXCOORD2;
[[vk::location(5)]] float3 WorldPos : TEXCOORD3;
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
    float4 sampled = g_textures[material.albedoIndex].Sample(samplers[material.albedoSamplerIndex], input.UV);
    float3 albedo = sampled.rgb * material.baseColorFactor.rgb * input.Color;
    float roughness = max(material.roughness, 0.04);
    float metallic = saturate(material.metallic);
    float ao = saturate(material.ao);

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

    float3 ambient = float3(0.4, 0.4, 0.4) * albedo * ao;
    float3 radiance = float3(10.0, 10.0, 10.0);
    float3 color = ambient + (diffuse + specular) * radiance * NdotL;

    return float4(color, sampled.a * material.baseColorFactor.a);
}

// Split-sum IBL PBR (Karis / liangairan brdf_pbr_by_ibl).
// MATERIAL_SET: material UBO + irradiance / specular cubemaps.
// FRAME_SET: BRDF LUT via bindless g_textures[brdfLutIndex].
// glTF ORM packing: G = roughness, B = metallic (R often packed AO).
// Compile variants via pixel options: ALPHA_BLEND=0 (opaque) / ALPHA_BLEND=1 (blend).
#ifndef ALPHA_BLEND
#define ALPHA_BLEND 0
#endif
#include "frame.hlsl"
#include "push_constant.hlsl"
#include "material.hlsl"

[[vk::binding(1, MATERIAL_SET)]] TextureCube irradianceMap : register(t0);
[[vk::binding(2, MATERIAL_SET)]] SamplerState sampler_irradianceMap : register(s0);
[[vk::binding(3, MATERIAL_SET)]] TextureCube specularMap : register(t1);
[[vk::binding(4, MATERIAL_SET)]] SamplerState sampler_specularMap : register(s1);

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
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 oneMinusRoughness = float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness);
    return F0 + (max(oneMinusRoughness, F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 ApplyNormalMap(float3 N, float4 tangentWS, float2 uv)
{
    if (material.normalIndex == 0xffffffff) {
        return normalize(N);
    }

    float3 tangentNormal = g_textures[material.normalIndex].Sample(
        g_samplers[material.normalSamplerIndex], uv).xyz * 2.0 - 1.0;

    float3 T = normalize(tangentWS.xyz);
    float3 B = cross(normalize(N), T) * tangentWS.w;
    float3x3 TBN = float3x3(T, B, normalize(N));
    // Row-vector * TBN (liangairan brdf_pbr_by_ibl).
    return normalize(mul(tangentNormal, TBN));
}

[[vk::push_constant]]
PushConstants pushConstants;

float4 main(VSOutput input) : SV_TARGET
{
    float4 sampled = g_textures[material.albedoIndex].Sample(
        g_samplers[material.albedoSamplerIndex], input.UV);
    float3 albedo = sampled.rgb * material.baseColorFactor.rgb * input.Color;

    float roughness = material.roughness;
    float metallic = material.metallic;
    float ao = material.ao;
    if (material.metallicRoughnessIndex != 0xffffffff) {
        float3 mr = g_textures[material.metallicRoughnessIndex].Sample(
            g_samplers[material.metallicRoughnessSamplerIndex], input.UV).rgb;
        ao *= mr.r;
        roughness *= mr.g;
        metallic *= mr.b;
    }
    roughness = max(roughness, 0.04);
    metallic = saturate(metallic);
    ao = saturate(ao);

    float3 N = ApplyNormalMap(input.Normal, input.Tangent, input.UV);
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

    float3 radiance = sunColor.rgb * sunIntensity;
    float3 color = (diffuse + specular) * radiance * NdotL;

    float3 F_ibl = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kS_ibl = F_ibl;
    float3 kD_ibl = (1.0 - kS_ibl) * (1.0 - metallic);

    float3 irradiance = irradianceMap.Sample(sampler_irradianceMap, N).rgb;
    float3 indirectDiffuse = irradiance * albedo * kD_ibl * ao;

    float specWidth = 0.0;
    float specHeight = 0.0;
    float specMips = 1.0;
    specularMap.GetDimensions(0, specWidth, specHeight, specMips);
    float maxReflectionLod = max(specMips - 1.0, 1.0);
    float3 R = reflect(-V, N);
    float3 prefiltered = specularMap.SampleLevel(
        sampler_specularMap, R, roughness * maxReflectionLod).rgb;

    float2 envBRDF = float2(1.0, 0.0);
    if (brdfLutIndex != 0xffffffff) {
        envBRDF = g_textures[brdfLutIndex].Sample(
            g_samplers[brdfLutSamplerIndex], float2(NdotV, roughness)).rg;
    }
    // Keep specular IBL unoccluded (liangairan); AO only on diffuse.
    float3 indirectSpecular = prefiltered * (F_ibl * envBRDF.x + envBRDF.y);

    color += indirectDiffuse + indirectSpecular;

#if ALPHA_BLEND
    // glTF BLEND: texture alpha * baseColorFactor.a (factor is the CPU opacity control).
    float alpha = sampled.a * material.baseColorFactor.a;
    alpha = material.baseColorFactor.a;
    return float4(color, alpha);
#else
    return float4(color, 1.0);
#endif
}

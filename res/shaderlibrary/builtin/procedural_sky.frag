// Godot-like ProceduralSkyMaterial: cheap sky/ground gradients + sun disk/halo.
// No textures. Sun direction/color/intensity come from frame global_ubo.

#include "frame.hlsl"
#include "descriptor_bindings.hlsl"

struct ProceduralSkyParams
{
    float4 sky_top_color;
    float4 sky_horizon_color;
    float4 ground_bottom_color;
    float4 ground_horizon_color;
    float inv_sky_curve;
    float inv_ground_curve;
    // Cosine of sun_angle_max (degrees). Glow ends here.
    float sun_angle_max;
    // Godot: 1.6 / pow(sun_curve, 1.4)
    float inv_sun_curve;
    // Cosine of sun angular radius (disk core).
    float sun_size;
    float exposure;
    float2 pad;
};

[[vk::binding(BIND_MATERIAL_UBO, MATERIAL_SET)]]
cbuffer material_ubo : register(b1)
{
    ProceduralSkyParams sky;
};

struct VSOutput
{
    [[vk::location(0)]] float2 ClipXY : TEXCOORD0;
};

float4 main(VSOutput input) : SV_TARGET
{
    // Ray from the camera through the far clip plane (same as skybox.frag).
    float4 world_pos = mul(invViewProjMatrix, float4(input.ClipXY, 1.0, 1.0));
    float3 world = world_pos.xyz / max(world_pos.w, 1e-6);
    float3 eye_dir = normalize(world - GetCameraPositionWS());

    float v_angle = clamp(eye_dir.y, -1.0, 1.0);

    // Sky: top → horizon (Godot ProceduralSkyMaterial).
    float3 color = lerp(
        sky.sky_top_color.rgb,
        sky.sky_horizon_color.rgb,
        saturate(pow(1.0 - v_angle, sky.inv_sky_curve)));

    // sunDirection = light travel direction; look toward -sunDirection for the disk.
    float3 sun_dir = normalize(-sunDirection.xyz);
    float sun_angle = dot(sun_dir, eye_dir);
    float3 sun_radiance = sunColor.rgb * sunIntensity;

    if (sun_angle > sky.sun_size) {
        color = sun_radiance;
    } else if (sun_angle > sky.sun_angle_max) {
        float c2 = (sky.sun_size - sun_angle) / max(sky.sun_size - sky.sun_angle_max, 1e-5);
        color = lerp(color, sun_radiance, saturate(pow(1.0 - c2, sky.inv_sun_curve)));
    }

    // Ground: bottom → horizon.
    float3 ground = lerp(
        sky.ground_bottom_color.rgb,
        sky.ground_horizon_color.rgb,
        saturate(pow(1.0 + v_angle, sky.inv_ground_curve)));

    color = lerp(ground, color, step(0.0, eye_dir.y)) * sky.exposure;

    // Cheap debanding (Godot render_mode use_debanding).
    float2 noise_uv = input.ClipXY * float2(0.5, 0.5) + 0.5;
    float dither = frac(sin(dot(noise_uv, float2(12.9898, 78.233))) * 43758.5453);
    color += (dither - 0.5) / 255.0;

    return float4(color, 1.0);
}

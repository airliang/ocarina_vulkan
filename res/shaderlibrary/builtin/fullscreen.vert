// Fullscreen triangle (SV_VertexID). Clip XY is passed through for view-ray reconstruction.

struct VSOutput
{
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float2 ClipXY : TEXCOORD0;
};

VSOutput main(uint vertex_id : SV_VertexID)
{
    // Standard fullscreen triangle UVs: (0,0), (2,0), (0,2)
    float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    // Vulkan NDC with Y flipped relative to D3D
    float2 clip_xy = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);

    // Far-plane clip position so depth writes 1.0 after perspective divide
    VSOutput output;
    output.Pos = float4(clip_xy, 1.0, 1.0);
    output.ClipXY = clip_xy;
    return output;
}

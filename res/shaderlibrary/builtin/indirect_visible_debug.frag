#include "material.hlsl"

struct PSInput
{
    float4 Pos : SV_POSITION;
};

// material.baseColorFactor keeps the material UBO in this pipeline so it can
// bind the same descriptor set as the indirect mesh (visibleIndices is binding 1).
float4 main(PSInput input) : SV_Target
{
    float alpha = material.baseColorFactor.a;
    return float4(1.0, 1.0, 1.0, alpha);
}

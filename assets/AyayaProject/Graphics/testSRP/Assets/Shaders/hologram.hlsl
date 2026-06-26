// hologram.hlsl — GDR-compatible via AyayaGDR.hlsl

// Push constant: must be declared BEFORE AyayaGDR.hlsl (used by GetAyayaVertex)
struct FrustumPC {
    float4 planes[6];
    uint instanceCount;
    uint lightModeMask;
    uint overrideInstanceID;
    uint _pad;
};
[[vk::push_constant]] FrustumPC pc;

#include "Generic/AyayaGDR.hlsl"

struct VSOutput { float4 position : SV_POSITION; float3 worldPos : TEXCOORD0; nointerpolation uint matIdx : TEXCOORD1; };

#ifdef VERTEX_SHADER
[[vk::binding(0, 0)]] cbuffer CameraUBO {
    float4x4 viewProj;
    float3 cameraPos;
    float time;
};

VSOutput main(uint vID : SV_VertexID, uint iID : SV_InstanceID) {
    AyayaVertex v = GetAyayaVertex(vID, iID);
    VSOutput o;
    float4 wPos = mul(v.worldMatrix, float4(v.position, 1.0));
    o.position = mul(viewProj, wPos);
    o.worldPos = wPos.xyz;
    o.matIdx = v.materialIdx;
    return o;
}
#else
[[vk::binding(0, 0)]] cbuffer CameraUBO { float4x4 viewProj; float3 cameraPos; float time; };
float4 main(VSOutput i) : SV_TARGET {
    GPUMaterial mat = u_Materials[i.matIdx];
    float4 holoColor = mat.customData[0];
    float scanSpeed = max(mat.customData[1].x, 0.1);
    if (dot(holoColor, holoColor) < 0.001) holoColor = float4(0.0, 0.8, 1.0, 0.5);
    float scanline = frac(i.worldPos.y * 10.0 - time * scanSpeed);
    float alpha = step(0.5, scanline) * holoColor.a;
    return float4(holoColor.rgb * alpha, 1.0);
}
#endif

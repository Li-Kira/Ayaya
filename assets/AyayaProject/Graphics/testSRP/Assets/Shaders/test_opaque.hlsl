// test_opaque.hlsl — GDR opaque rendering test shader for GBuffer target
#include "Generic/AyayaGDR.hlsl"

struct VSOutput {
    float4 position      : SV_POSITION;
    float3 worldPos      : TEXCOORD0;
    float3 worldNormal   : TEXCOORD1;
    float2 uv            : TEXCOORD2;
    nointerpolation uint matIdx : TEXCOORD3;
};

#ifdef VERTEX_SHADER
VSOutput main(uint vID : SV_VertexID, uint iID : SV_InstanceID) {
    AyayaVertex v = GetAyayaVertex(vID, iID);
    VSOutput o;
    float4 wPos = mul(v.worldMatrix, float4(v.position, 1.0));
    o.position    = mul(viewProj, wPos);
    o.worldPos    = wPos.xyz;
    o.worldNormal = normalize(mul((float3x3)v.worldMatrix, v.normal));
    o.uv          = v.uv;
    o.matIdx      = v.materialIdx;
    return o;
}
#else
// GBuffer layout:
//   SV_TARGET0: RG16F  — octahedral encoded normal
//   SV_TARGET1: RGBA8  — albedo.rgb + roughness
//   SV_TARGET2: RGBA8  — metallic + ao + flags
//   SV_TARGET3: RGBA8  — shadow/selection/clipZ

float2 OctEncode(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 e = (n.z >= 0.0) ? n.xy : (float2(1.0, 1.0) - abs(n.yx)) * sign(n.xy);
    return e * 0.5 + 0.5;
}

struct PSOutput {
    float2 normal   : SV_TARGET0;
    float4 albedo   : SV_TARGET1;
    float4 pbr      : SV_TARGET2;
    float4 custom   : SV_TARGET3;
};

PSOutput main(VSOutput i) {
    PSOutput o;

    // Cosine palette → albedo
    float3 col = 0.5 + 0.5 * cos(_Time.y + i.uv.xyx + float3(0.0, 2.0, 4.0));

    o.normal = OctEncode(normalize(i.worldNormal));
    o.albedo = float4(col, 0.5);       // RGB=albedo, A=roughness
    o.pbr    = float4(0.0, 1.0, 0.0, 1.0); // metallic=0, ao=1, flags=0
    o.custom = float4(1.0, 0.0, 0.0, 1.0); // receiveShadows=1

    return o;
}
#endif

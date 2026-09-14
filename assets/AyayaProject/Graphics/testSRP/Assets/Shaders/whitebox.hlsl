// whitebox.hlsl — world-space tri-planar whitebox prototype material.
// UV == world position (meters), so the texture tiles 1:1 with real-world scale:
// 1 m = one full texture, 1.5 m = 1.5 tiles, 2 m = 2 tiles (Repeat wrap).
#include "Generic/AyayaGDR.hlsl"

struct VSOutput {
    float4 position      : SV_POSITION;
    float3 worldPos      : TEXCOORD0;
    float3 worldNormal   : TEXCOORD1;
    float2 uv            : TEXCOORD2;
    nointerpolation uint matIdx : TEXCOORD3;
    float2 velocity      : TEXCOORD4;  // per-object motion vector (UE5-style)
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

    // Per-object motion vector (camera + object motion). Matches gbuffer_gdr.vert's
    // UV convention (negative-viewport Y-flip): motion = currentUV - prevUV.
    uint finalID = (pc.overrideInstanceID != 0xFFFFFFFFu) ? pc.overrideInstanceID : iID;
    float4 prevPos  = mul(u_Instances[finalID].prevTransform, float4(v.position, 1.0));
    float4 curClip  = mul(viewProj, wPos);
    float4 prevClip = mul(prevViewProjection, prevPos);
    float2 curNDC   = curClip.xy / curClip.w;
    float2 prevNDC  = prevClip.xy / prevClip.w;
    float2 curUV    = float2(curNDC.x * 0.5 + 0.5, 1.0 - (curNDC.y * 0.5 + 0.5));
    float2 prevUV   = float2(prevNDC.x * 0.5 + 0.5, 1.0 - (prevNDC.y * 0.5 + 0.5));
    o.velocity      = curUV - prevUV;

    return o;
}
#else
// GBuffer layout (matches the shared "GBuffer" target):
//   SV_TARGET0: RG16F  — octahedral encoded normal
//   SV_TARGET1: RGBA8  — albedo.rgb + roughness
//   SV_TARGET2: RGBA8  — metallic + ao + flags
//   SV_TARGET3: RGBA8  — shadow/selection/clipZ
//   SV_TARGET4: RG16F  — per-object velocity (motion vector)

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
    float2 velocity : SV_TARGET4;
};

PSOutput main(VSOutput i) {
    PSOutput o;
    GPUMaterial mat = u_Materials[i.matIdx];

    float3 N = normalize(i.worldNormal);

    // ── World-space tri-planar UV ──
    // Each world plane is sampled with uv = worldPos (in meters), blended by the
    // world normal. uv.z/x/y weighting: XY plane normal is +Z, YZ normal is +X,
    // XZ normal is +Y.
    float3 w = abs(N);
    w /= (w.x + w.y + w.z + 1e-5);

    float3 cXY = SampleGlobalTexture((uint)mat.albedoBindless, i.worldPos.xy).rgb; // weight w.z
    float3 cYZ = SampleGlobalTexture((uint)mat.albedoBindless, i.worldPos.yz).rgb; // weight w.x
    float3 cXZ = SampleGlobalTexture((uint)mat.albedoBindless, i.worldPos.xz).rgb; // weight w.y
    float3 tex = cXY * w.z + cYZ * w.x + cXZ * w.y;

    o.normal   = OctEncode(N);
    o.albedo   = float4(mat.albedo.rgb * tex, 1.0);              // alpha = 1.0
    o.pbr      = float4(mat.metallic, mat.roughness, mat.ao, 1.0); // R=metallic G=roughness B=ao
    o.custom   = float4(1.0, 0.0, 0.0, 1.0); // receiveShadows=1
    o.velocity = i.velocity;

    return o;
}
#endif

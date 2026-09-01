// water.hlsl — GDR transparent water (Gerstner + fresnel + refraction + IBL reflection + depth absorption).
// Rendered by GenericDrawPass with ReadSceneColor/ReadSceneDepth/ReadIBL = true.
#include "Generic/AyayaGDR.hlsl"
#include "Generic/AyayaWater.hlsl"

// ── Set 3: scene inputs (bound by GenericDrawPass scene-input descriptor set) ──
[[vk::combinedImageSampler]][[vk::binding(0, 3)]] Texture2D    u_SceneColor     : register(t0, space3);
[[vk::combinedImageSampler]][[vk::binding(0, 3)]] SamplerState u_SceneColorSamp : register(s0, space3);
[[vk::combinedImageSampler]][[vk::binding(1, 3)]] Texture2D    u_SceneDepth     : register(t1, space3);
[[vk::combinedImageSampler]][[vk::binding(1, 3)]] SamplerState u_SceneDepthSamp : register(s1, space3);
[[vk::combinedImageSampler]][[vk::binding(2, 3)]] TextureCube  u_PrefilterMap   : register(t2, space3);
[[vk::combinedImageSampler]][[vk::binding(2, 3)]] SamplerState u_PrefilterSamp  : register(s2, space3);
[[vk::combinedImageSampler]][[vk::binding(3, 3)]] Texture2D    u_BRDFLUT        : register(t3, space3);
[[vk::combinedImageSampler]][[vk::binding(3, 3)]] SamplerState u_BRDFLUTSamp    : register(s3, space3);

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

    GPUMaterial mat = u_Materials[v.materialIdx];

    // World position (water plane is XZ, Y-up)
    float3 wPos = mul(v.worldMatrix, float4(v.position, 1.0)).xyz;

    // ── Two Gerstner waves (params packed in customData) ──
    float3 tangent  = float3(1.0, 0.0, 0.0);
    float3 binormal = float3(0.0, 0.0, 1.0);

    float4 w0 = mat.customData[0];  // (amp0, len0, steep0, speed0)
    float4 w1 = mat.customData[1];  // (dir0.x, dir0.y, amp1, len1)
    float4 w2 = mat.customData[2];  // (steep1, speed1, dir1.x, dir1.y)

    wPos += GerstnerWave(w1.xy, w0.z, w0.y, w0.x, w0.w, wPos.xz, _Time.y, tangent, binormal);   // wave 0: dir=w1.xy, steep=w0.z, len=w0.y, amp=w0.x, speed=w0.w
    wPos += GerstnerWave(w2.zw, w2.x, w1.w, w1.z, w2.y, wPos.xz, _Time.y, tangent, binormal);   // wave 1: dir=w2.zw, steep=w2.x, len=w1.w, amp=w1.z, speed=w2.y

    float3 N = normalize(cross(binormal, tangent));

    o.position    = mul(viewProj, float4(wPos, 1.0));
    o.worldPos    = wPos;
    o.worldNormal = N;
    o.uv          = v.uv;
    o.matIdx      = v.materialIdx;
    return o;
}
#else
float4 main(VSOutput i) : SV_TARGET {
    GPUMaterial mat = u_Materials[i.matIdx];

    float3 N = normalize(i.worldNormal);
    float3 V = normalize(cameraPos - i.worldPos);
    float NdV = saturate(dot(N, V));

    // ── Fresnel ──
    float fresnel = WaterFresnel(NdV, mat.customData[3].z);

    // ── Refraction (sample scene color behind water, offset by surface normal) ──
    float2 screenUV = i.position.xy * pc._TexelSize.xy;   // pixel → UV
    float2 refractUV = screenUV + N.xz * mat.customData[3].w * 0.02;
    float3 refractColor = u_SceneColor.Sample(u_SceneColorSamp, refractUV).rgb;

    // ── IBL reflection (split-sum) ──
    float3 R = reflect(-V, N);
    float roughness = clamp(mat.roughness, 0.02, 1.0);
    float3 prefiltered = u_PrefilterMap.SampleLevel(u_PrefilterSamp, R, roughness * 4.0).rgb;
    float2 brdf = u_BRDFLUT.Sample(u_BRDFLUTSamp, float2(NdV, roughness)).rg;
    float3 reflectColor = prefiltered * (fresnel * brdf.x + brdf.y);

    // ── Depth absorption ──
    float waterDepth = i.position.z / max(i.position.w, 1e-5);
    float sceneDepth = u_SceneDepth.Sample(u_SceneDepthSamp, screenUV).r;
    float waterThickness = saturate((sceneDepth - waterDepth) * mat.customData[3].w);

    float3 shallowColor = mat.albedo.rgb;
    float3 deepColor    = shallowColor * mat.customData[3].x;   // darker / bluer
    float3 waterColor   = lerp(shallowColor, deepColor, waterThickness);

    // ── Edge foam (shallow water near shore) ──
    float foam = saturate((mat.customData[3].y - waterThickness) * 4.0);
    waterColor = lerp(waterColor, float3(1.0, 1.0, 1.0), foam * 0.6);

    // ── Composite: fresnel blend of refraction and reflection ──
    float3 color = lerp(refractColor, reflectColor, fresnel);
    color *= waterColor;

    // Water surface is opaque: the scene behind is already captured by refraction.
    // alpha=1 marks "water present" so the composite (alpha blend) replaces the scene
    // here, while the cleared (alpha=0) region leaves the sky/scene untouched.
    return float4(color, 1.0);
}
#endif

// water.hlsl — Backrooms pool water (GDR transparent).
// Techniques: Gerstner ripples + high-frequency ripple detail, screen-space refraction,
// procedural caustics, Beer-Lambert depth absorption, Fresnel, IBL split-sum reflection.
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

    // ── Two Gerstner ripples (calm pool — small amplitude, params packed in customData) ──
    float3 tangent  = float3(1.0, 0.0, 0.0);
    float3 binormal = float3(0.0, 0.0, 1.0);

    float4 w0 = mat.customData[0];  // (amp0, len0, steep0, speed0)
    float4 w1 = mat.customData[1];  // (dir0.x, dir0.y, amp1, len1)
    float4 w2 = mat.customData[2];  // (steep1, speed1, dir1.x, dir1.y)

    wPos += GerstnerWave(w1.xy, w0.z, w0.y, w0.x, w0.w, wPos.xz, _Time.y, tangent, binormal);   // wave 0
    wPos += GerstnerWave(w2.zw, w2.x, w1.w, w1.z, w2.y, wPos.xz, _Time.y, tangent, binormal);   // wave 1

    float3 N = normalize(cross(binormal, tangent));

    // High-frequency ripple detail — breaks up the specular so the water shimmers
    // instead of mirroring (avoids the uniform "oil film" look). Kept subtle so it
    // doesn't fight the Gerstner waves or read as noise.
    N = normalize(lerp(N, RippleDetailNormal(wPos.xz, _Time.y), 0.15));

    o.position    = mul(viewProj, float4(wPos, 1.0));
    o.worldPos    = wPos;
    o.worldNormal = N;
    o.uv          = v.uv;
    o.matIdx      = v.materialIdx;
    return o;
}
#else
// Screen-space reflection — view-space ray march to reflect the actual room (ceiling,
// tiles, walls) instead of only the IBL skybox. References ssr_march.frag's approach.
float3 ScreenSpaceReflection(float3 worldPos, float3 N, float3 R_world, out float hit) {
    hit = 0.0;
    float3 viewPos = mul(view, float4(worldPos, 1.0)).xyz;   // view space (z < 0 in front)
    float3 R_view  = mul((float3x3)view, R_world);
    float3 N_view  = mul((float3x3)view, N);

    float3 rayOrigin = viewPos + N_view * 0.02;   // bias along normal (avoid self-hit)
    float3 rayDir    = normalize(R_view);
    float  stepSize  = 0.2;
    int    maxSteps  = 48;

    float3 rayPos = rayOrigin;
    for (int i = 0; i < maxSteps; i++) {
        rayPos += rayDir * stepSize;

        float4 clip = mul(projection, float4(rayPos, 1.0));
        float2 ndc  = clip.xy / clip.w;
        float2 uv   = float2(ndc.x * 0.5 + 0.5, 1.0 - (ndc.y * 0.5 + 0.5));
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;

        float sceneNDC = u_SceneDepth.Sample(u_SceneDepthSamp, uv).r;
        if (sceneNDC >= 1.0) continue;   // sky → keep marching

        float sceneDepth = LinearizeDepth(sceneNDC);
        float rayDepth   = -rayPos.z;
        if (rayDepth > sceneDepth) {     // ray behind the surface → hit
            hit = 1.0;
            return u_SceneColor.Sample(u_SceneColorSamp, uv).rgb;
        }
    }
    return float3(0.0, 0.0, 0.0);
}

float4 main(VSOutput i) : SV_TARGET {
    GPUMaterial mat = u_Materials[i.matIdx];

    float3 V = normalize(cameraPos - i.worldPos);
    float3 N = normalize(i.worldNormal);
    float NdV = saturate(dot(N, V));

    // ── Fresnel (water F0 ~0.02) ──
    float fresnel = WaterFresnel(NdV, mat.customData[3].z);

    float2 screenUV = i.position.xy * pc._TexelSize.xy;   // pixel → UV

    // ── Linear depth → water thickness (meters) ──
    float waterNDC   = i.position.z;  // SV_Position.z is already NDC depth [0,1]
    float sceneNDC   = u_SceneDepth.Sample(u_SceneDepthSamp, screenUV).r;
    float waterDepth = LinearizeDepth(waterNDC);
    float sceneDepth = LinearizeDepth(sceneNDC);
    // Clamp the thickness so grazing-angle rays (which travel a long diagonal path
    // through the water) don't absorb far more than the actual ~2m pool depth — keeps
    // the water clear and consistent regardless of camera angle.
    float waterThickness = min(max(sceneDepth - waterDepth, 0.0), 4.0);

    // ── Refraction: screen-space offset from the view-space normal ──
    // view +X → screen +X; view +Y (up) → NDC +Y = screen top → screenUV.y decreases, so negate Y.
    float3 N_view = mul((float3x3)view, N);
    float2 refractUV = screenUV + float2(N_view.x, -N_view.y) * mat.customData[3].w;

    // Depth guard: if the refracted UV samples the sky (depth==1) or something IN FRONT of
    // the water (wall), fall back to the original UV — prevents the screen-space refraction
    // leak (bright block at the water edge).
    float refractedNDC = u_SceneDepth.Sample(u_SceneDepthSamp, refractUV).r;
    if (refractedNDC >= 1.0 || refractedNDC < waterNDC) {
        refractUV = screenUV;
    }

    float3 refractColor = u_SceneColor.Sample(u_SceneColorSamp, refractUV).rgb;

    // ── Caustics: light focusing pattern cast on the pool floor ──
    // Applied to the refracted floor; fades with depth (deeper water scatters the focused light).
    float3 caustic = PoolCaustics(i.worldPos.xz, _Time.y, mat.customData[0], mat.customData[1], mat.customData[2]);
    float causticFade = exp(-waterThickness * 0.5);
    refractColor = refractColor * (1.0 + caustic * 0.5 * causticFade);

    // ── Depth absorption (Beer-Lambert exp(-σ·d)) — subtle pool water ──
    // Gentle σ: red absorbs a bit more → a soft blue-green tint with depth, while the
    // pool bottom stays clearly visible (and the 4m clamp keeps grazing angles consistent).
    float3 deepColor    = mat.albedo.rgb * mat.customData[3].x;   // water tint at depth
    float3 absorption   = float3(0.12, 0.04, 0.012);              // gentle blue-green tint
    float3 transmittance = exp(-absorption * waterThickness);

    // ── Edge foam (shallow water near shore): push transmittance → 1 (transparent) ──
    float foam = saturate((mat.customData[3].y - waterThickness) * 2.0);
    transmittance = lerp(transmittance, 1.0, foam);

    // Refraction body: caustic-lit floor gently tinted toward the water color with depth.
    float3 refractTinted = refractColor * transmittance + deepColor * (1.0 - transmittance);

    // ── Reflection: SSR (room) + IBL (skybox fallback) ──
    // SSR reflects the actual room (ceiling/tiles/walls); IBL fills in missed rays.
    float3 R = reflect(-V, N);
    float roughness = clamp(mat.roughness, 0.02, 1.0);
    float3 prefiltered = u_PrefilterMap.SampleLevel(u_PrefilterSamp, R, roughness * 4.0).rgb;
    float2 brdf = u_BRDFLUT.Sample(u_BRDFLUTSamp, float2(NdV, roughness)).rg;
    float3 iblColor = prefiltered * pc._EnvironmentIntensity * (fresnel * brdf.x + brdf.y);

    float  ssrHit;
    float3 ssrColor = ScreenSpaceReflection(i.worldPos, N, R, ssrHit);
    // Fade SSR near screen edges (ray march is unreliable there → fall back to IBL).
    float ssrEdge = smoothstep(0.0, 0.1, screenUV.x) * smoothstep(0.0, 0.1, 1.0 - screenUV.x)
                  * smoothstep(0.0, 0.1, screenUV.y) * smoothstep(0.0, 0.1, 1.0 - screenUV.y);
    ssrHit *= ssrEdge;
    float3 reflectColor = lerp(iblColor, ssrColor, ssrHit);   // SSR hit → reflect the room

    // ── Composite: fresnel blend of (caustic + absorbed + tinted) refraction and reflection ──
    // Only the refraction (water body) is absorbed + tinted; the specular reflection is not.
    float3 color = lerp(refractTinted, reflectColor, fresnel);

    return float4(color, 1.0);
}
#endif

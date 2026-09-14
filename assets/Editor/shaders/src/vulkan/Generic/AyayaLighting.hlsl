// ==========================================
// AyayaLighting.hlsl — shared PBR lighting for GenericDrawPass (forward) shaders
//
// Mirrors the deferred LightingPass (deferred_lighting.frag) so forward/custom
// effects get the SAME lighting as GBuffer materials.
//
// Include AFTER Generic/AyayaGDR.hlsl (needs GPUMaterial + DirLight cbuffer).
// The caller samples the IBL textures itself and passes the values in.
// ==========================================

#define AYAYA_PI 3.14159265359

float3 AyayaF0(float3 albedo, float metallic) {
    return lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
}

float AyayaD_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(AYAYA_PI * d * d, 1e-5);
}

float AyayaG_Schlick(float NdotX, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return NdotX / max(NdotX * (1.0 - k) + k, 1e-5);
}

float AyayaG_Smith(float NdotV, float NdotL, float roughness) {
    return AyayaG_Schlick(NdotV, roughness) * AyayaG_Schlick(NdotL, roughness);
}

float3 AyayaF_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float3 AyayaF_SchlickR(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0)
              * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Cook-Torrance direct light (mirrors deferred_lighting.frag:136-145)
float3 ComputeDirectLight(float3 N, float3 V, float3 L, float3 lightColor,
                          float3 albedo, float3 F0, float roughness, float metallic) {
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float D = AyayaD_GGX(max(dot(N, H), 0.0), roughness);
    float G = AyayaG_Smith(NdotV, NdotL, roughness);
    float3 F = AyayaF_Schlick(max(dot(H, V), 0.0), F0);
    float3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);
    return (kD * albedo / AYAYA_PI + spec) * lightColor * NdotL;
}

// IBL: diffuse irradiance + specular split-sum (mirrors deferred_lighting.frag:147-158)
float3 ComputeIBL(float3 N, float3 V, float3 albedo, float3 F0, float roughness,
                  float metallic, float3 irradiance, float3 prefiltered, float2 brdf) {
    float NdV = max(dot(N, V), 0.0);
    float3 F = AyayaF_SchlickR(NdV, F0, roughness);
    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);
    float3 spI = prefiltered * (F * brdf.x + brdf.y);
    float3 amb = kD * irradiance * albedo;
    return amb + spI;
}

// Combined lighting, dispatched by the material's ShadingModel:
//   0 = Lit  — directional light + IBL (full PBR)
//   1 = Unlit — return albedo/emissive directly (opts out of lighting)
float3 ComputeShading(GPUMaterial mat, float3 N, float3 V, float3 albedo,
                      float3 irradiance, float3 prefiltered, float2 brdf) {
    if (mat.shadingModel == 1) {
        return albedo;
    }

    float metallic  = saturate(mat.metallic);
    float roughness = clamp(mat.roughness, 0.04, 1.0);
    float ao        = saturate(mat.ao);
    float3 F0 = AyayaF0(albedo, metallic);

    float3 L = normalize(-DirLightDir.xyz);
    float3 direct = ComputeDirectLight(N, V, L, DirLightColor.rgb,
                                       albedo, F0, roughness, metallic);

    float3 ibl = ComputeIBL(N, V, albedo, F0, roughness, metallic,
                            irradiance, prefiltered, brdf) * ao;

    return direct + ibl;
}

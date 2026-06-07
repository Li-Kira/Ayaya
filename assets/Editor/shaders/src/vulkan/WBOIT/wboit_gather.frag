#version 450 core
// WBOIT Gather Pass — forward PBR with IBL, matching deferred lighting quality

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;

layout(location = 0) out vec4 out_Accumulation;
layout(location = 1) out float out_Revealage;

// set=0: global UBOs (Camera + LightData)
layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;
    vec3 u_CameraPosition;
};
layout(set = 0, binding = 1) uniform LightData {
    vec4 DirLightDir;
    vec4 DirLightColor;
    vec4 PointLights[8];
    int PointLightCount;
};

// set=1: per-material textures (mirrors deferred lighting)
layout(set = 1, binding = 0) uniform sampler2D   u_AlbedoMap;
layout(set = 1, binding = 1) uniform sampler2D   u_MetallicMap;
layout(set = 1, binding = 2) uniform sampler2D   u_RoughnessMap;
layout(set = 1, binding = 3) uniform samplerCube u_IrradianceMap;
layout(set = 1, binding = 4) uniform samplerCube u_PrefilteredMap;
layout(set = 1, binding = 5) uniform sampler2D   u_BRDFLUT;
layout(set = 1, binding = 6) uniform sampler2D   u_NormalMap;
layout(set = 1, binding = 7) uniform sampler2D   u_ORMMap;
layout(set = 1, binding = 8) uniform sampler2D   u_AOMap;

layout(push_constant) uniform PC {
    mat4  u_Transform;
    vec4  u_Albedo;
    float u_Metallic;
    float u_Roughness;
    float u_AO;
    int   u_UseAlbedoMap;
    int   u_UseNormalMap;
    int   u_UseORMMap;
    int   u_UseMetallicMap;
    int   u_UseRoughnessMap;
    int   u_UseAOMap;
    float u_Alpha;
} pc;

const float PI = 3.14159265359;

// Pre-exposure scale to keep HDR accumulation within FP16 range.
// PBR litColor can reach ~60k+ with 100klux sunlight; weight ~10x near camera.
// 60k * 10 = 600k > 65504 (FP16 max) → overflow.  0.01 scale → safe up to 6.5M.
// The resolve pass divides by the same factor to recover the original HDR value.
const float WBOIT_PRE_EXPOSURE = 0.01;

float DistributionGGX(vec3 N, vec3 H, float r) {
    float a=r*r, a2=a*a; float NdH=max(dot(N,H),0.0);
    float d=(NdH*NdH*(a2-1.0)+1.0); return a2/(PI*d*d);
}
float GeometrySchlickGGX(float NdV, float r) {
    float k=(r+1.0)*(r+1.0)/8.0; return NdV/(NdV*(1.0-k)+k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float r) {
    return GeometrySchlickGGX(max(dot(N,V),0.0),r)*GeometrySchlickGGX(max(dot(N,L),0.0),r);
}
vec3 fresnelSchlick(float c, vec3 F0) {
    return F0+(1.0-F0)*pow(clamp(1.0-c,0.0,1.0),5.0);
}
vec3 fresnelSchlickRoughness(float c, vec3 F0, float r) {
    return F0+(max(vec3(1.0-r),F0)-F0)*pow(clamp(1.0-c,0.0,1.0),5.0);
}

float CalcWeight(float depth, float alpha) {
    return alpha * max(0.1, 10.0 * (1.0 - depth));
}

void main() {
    // Material
    float alpha  = pc.u_Alpha;
    vec3  Albedo = pc.u_Albedo.rgb;
    float AO        = pc.u_AO;
    float Metallic  = pc.u_Metallic;
    float Roughness = max(pc.u_Roughness, 0.04);

    if (pc.u_UseAlbedoMap == 1) {
        vec4 tex = texture(u_AlbedoMap, v_TexCoord);
        Albedo *= tex.rgb;
        alpha  *= tex.a;
    }

    // ORM packed texture (UE4 convention: R=AO, G=Roughness, B=Metallic)
    if (pc.u_UseORMMap == 1) {
        vec3 orm = texture(u_ORMMap, v_TexCoord).rgb;
        AO        = orm.r;
        Roughness = max(orm.g * pc.u_Roughness, 0.04);
        Metallic  = orm.b * pc.u_Metallic;
    } else {
        if (pc.u_UseAOMap       == 1) AO        *= texture(u_AOMap,       v_TexCoord).r;
        if (pc.u_UseMetallicMap  == 1) Metallic  *= texture(u_MetallicMap,  v_TexCoord).r;
        if (pc.u_UseRoughnessMap == 1) Roughness *= texture(u_RoughnessMap, v_TexCoord).r;
        Roughness = max(Roughness, 0.04);
    }

    vec3 N = normalize(v_Normal);
    vec3 V = normalize(u_CameraPosition - v_WorldPos);
    vec3 F0 = mix(vec3(0.04), Albedo, Metallic);

    // ---- Directional light ----
    vec3 Lo = vec3(0.0);
    {
        vec3 L = normalize(-DirLightDir.xyz);
        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float D = DistributionGGX(N, H, Roughness);
        float G = GeometrySmith(N, V, L, Roughness);
        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3  spec = (D * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 0.001);
        vec3  kS = F;
        vec3  kD = (1.0 - kS) * (1.0 - Metallic);
        Lo += (kD * Albedo / PI + spec) * DirLightColor.rgb * NdotL;
    }

    // ---- IBL ambient ----
    vec3 ambient = vec3(0.0);
    {
        float NdotV = max(dot(N, V), 0.0);
        vec3 F = fresnelSchlickRoughness(NdotV, F0, Roughness);
        vec3 kS = F;
        vec3 kD = (1.0 - kS) * (1.0 - Metallic);

        vec3 irradiance = texture(u_IrradianceMap, N).rgb;
        vec3 diffuseIBL = kD * Albedo * irradiance;

        vec3 R = reflect(-V, N);
        vec3 prefiltered = textureLod(u_PrefilteredMap, R, Roughness * 4.0).rgb;
        vec2 brdf = texture(u_BRDFLUT, vec2(max(NdotV, 1e-5), Roughness)).rg;
        vec3 specularIBL = prefiltered * (F * brdf.x + brdf.y);

        ambient = diffuseIBL + specularIBL;
    }

    vec3 litColor = ambient + Lo;

    // WBOIT accumulation — pre-exposure scaled to avoid FP16 overflow
    vec3 premulColor = litColor * alpha;
    float weight = CalcWeight(gl_FragCoord.z, alpha);

    out_Accumulation = vec4(premulColor * weight * WBOIT_PRE_EXPOSURE, alpha * weight);
    out_Revealage = alpha;
}

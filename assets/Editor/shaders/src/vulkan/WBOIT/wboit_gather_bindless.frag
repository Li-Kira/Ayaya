#version 450 core
#extension GL_EXT_nonuniform_qualifier : require
// WBOIT Gather Pass (Bindless) — forward PBR with IBL, matching deferred lighting quality

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;

layout(location = 0) out vec4 out_Accumulation;
layout(location = 1) out float out_Revealage;

// set=0: global UBOs (Camera + LightData)
layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;
    mat4 u_View;
    vec3 u_CameraPosition;
};
layout(set = 0, binding = 1) uniform LightData {
    vec4 DirLightDir;
    vec4 DirLightColor;
    vec4 PointLights[8];
    int PointLightCount;
};

// set=1: bindless material texture array (2D only)
layout(set = 1, binding = 0) uniform sampler2D u_GlobalTextures[];

// set=3: IBL textures (bound once per frame, not per-material)
layout(set = 3, binding = 0) uniform samplerCube u_IrradianceMap;
layout(set = 3, binding = 1) uniform samplerCube u_PrefilteredMap;
layout(set = 3, binding = 2) uniform sampler2D   u_BRDFLUT;

layout(push_constant) uniform PC {
    mat4   u_Transform;                        // offset 0   (64B)
    vec4   u_Albedo;                           // offset 64  (16B)
    float  u_Metallic;                         // offset 80  (4B)
    float  u_Roughness;                        // offset 84  (4B)
    float  u_AO;                               // offset 88  (4B)
    uint   u_UseORMMap;                        // offset 92  (4B)
    uint   u_AlbedoMapIndex;                   // offset 96  (4B)
    uint   u_NormalMapIndex;                   // offset 100 (4B)
    uint   u_ORMMapIndex;                      // offset 104 (4B)
    uint   u_MetallicMapIndex;                 // offset 108 (4B)
    uint   u_RoughnessMapIndex;                // offset 112 (4B)
    uint   u_AOMapIndex;                       // offset 116 (4B)
    float  u_Alpha;                            // offset 120 (4B)
} pc;

const float PI = 3.14159265359;
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
    float alpha  = pc.u_Alpha;
    vec3  Albedo = pc.u_Albedo.rgb;
    float AO        = pc.u_AO;
    float Metallic  = pc.u_Metallic;
    float Roughness = max(pc.u_Roughness, 0.04);

    // Always sample albedo — default index guarantees valid texture
    vec4 tex = texture(u_GlobalTextures[nonuniformEXT(pc.u_AlbedoMapIndex)], v_TexCoord);
    Albedo *= tex.rgb;
    alpha  *= tex.a;

    if (pc.u_UseORMMap != 0u) {
        vec3 orm = texture(u_GlobalTextures[nonuniformEXT(pc.u_ORMMapIndex)], v_TexCoord).rgb;
        AO        = orm.r;
        Roughness = max(orm.g * pc.u_Roughness, 0.04);
        Metallic  = orm.b * pc.u_Metallic;
    } else {
        Metallic  *= texture(u_GlobalTextures[nonuniformEXT(pc.u_MetallicMapIndex)],  v_TexCoord).r;
        Roughness *= texture(u_GlobalTextures[nonuniformEXT(pc.u_RoughnessMapIndex)], v_TexCoord).r;
        AO        *= texture(u_GlobalTextures[nonuniformEXT(pc.u_AOMapIndex)],        v_TexCoord).r;
        Roughness = max(Roughness, 0.04);
    }

    vec3 N = normalize(v_Normal);
    vec3 V = normalize(u_CameraPosition - v_WorldPos);
    vec3 F0 = mix(vec3(0.04), Albedo, Metallic);

    // Directional light
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

    // IBL ambient (set=3, unchanged)
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

    vec3 premulColor = litColor * alpha;
    float weight = CalcWeight(gl_FragCoord.z, alpha);

    out_Accumulation = vec4(premulColor * weight * WBOIT_PRE_EXPOSURE, alpha * weight);
    out_Revealage = alpha;
}

#version 450 core
// fog_apply.frag — composite volumetric fog onto the HDR scene color.
// Samples the integrated froxel volume at the pixel's linear depth and applies
//   color = sceneColor * transmittance + inScattering.
// Fullscreen pass; vertex shader is PostProcess/postprocess.vert.

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 v_TexCoord;

layout(set = 1, binding = 0) uniform sampler2D u_SceneColor;
layout(set = 1, binding = 1) uniform sampler2D u_SceneDepth;
layout(set = 2, binding = 0) uniform sampler3D u_FogVolume;

layout(push_constant) uniform FogPC {
    vec4 NearFar;   // x = near, y = far, z = NoFog flag (pass-through)
} pc;

const float FROXEL_Z = 64.0;

void main() {
    vec3 sceneColor = texture(u_SceneColor, v_TexCoord).rgb;
    if (pc.NearFar.z > 0.5) { FragColor = vec4(sceneColor, 1.0); return; }   // no fog → pass-through

    float ndc = texture(u_SceneDepth, v_TexCoord).r;

    float n = pc.NearFar.x;
    float f = pc.NearFar.y;

    // Map linear depth → froxel slice (exponential distribution, inverse of injection).
    float iz;
    if (ndc >= 1.0) {
        iz = FROXEL_Z - 1.0;   // sky → integrate full fog distance
    } else {
        float linearDepth = n * f / max(f - ndc * (f - n), 1e-5);
        iz = log(max(linearDepth / n, 1.0)) / log(f / n) * (FROXEL_Z - 1.0);
        iz = clamp(iz, 0.0, FROXEL_Z - 1.0);
    }

    vec4 fog = texture(u_FogVolume, vec3(v_TexCoord, (iz + 0.5) / FROXEL_Z));
    vec3 result = sceneColor * fog.a + fog.rgb;
    FragColor = vec4(result, 1.0);
}

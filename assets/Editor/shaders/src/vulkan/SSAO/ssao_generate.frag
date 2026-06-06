#version 450 core
layout(location = 0) out float o_AO;
layout(location = 0) in vec2 v_TexCoord;

// Camera UBO (provides u_ViewProjection for world→clip projection)
layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;
    vec3 u_CameraPosition;
};

// GBuffer inputs: world-space position (RGBA32F) and normal (RGBA16F)
layout(set = 1, binding = 0) uniform sampler2D u_Position;
layout(set = 1, binding = 1) uniform sampler2D u_Normal;
layout(set = 1, binding = 2) uniform sampler2D u_Noise;

layout(push_constant) uniform PC {
    vec2  u_NoiseScale;
    float u_Radius;
    float u_Bias;
    float u_Power;
    int   u_SampleCount;
} pc;

void main() {
    vec3 worldPos = texture(u_Position, v_TexCoord).xyz;
    vec3 normal   = texture(u_Normal, v_TexCoord).xyz;

    // Skip skybox / background (position contains garbage, normal ~zero)
    if (length(normal) < 0.1) { o_AO = 1.0; return; }

    normal = normalize(normal);

    // Build TBN in world-space using noise texture for random rotation
    vec3 rvec = texture(u_Noise, v_TexCoord * pc.u_NoiseScale).xyz * 2.0 - 1.0;
    vec3 tangent   = normalize(rvec - normal * dot(rvec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    float radius = pc.u_Radius;
    float bias   = pc.u_Bias;
    int samples  = pc.u_SampleCount;

    for (int i = 0; i < 64; i++) {
        if (i >= samples) break;

        // Pseudo-random hemisphere samples in tangent space
        vec3 sampleOffset = TBN * vec3(
            sin(float(i) * 12.9898 + float(i) * 78.233) * 0.5 + sin(float(i) * 45.164) * 0.5,
            cos(float(i) * 37.719 + float(i) * 17.371) * 0.5 + cos(float(i) * 93.145) * 0.5,
            float(i + 1) / float(samples) * 0.5 + 0.5
        );
        sampleOffset = normalize(sampleOffset) * (float(i) / float(samples) * 0.5 + 0.5);

        vec3 sampleWorldPos = worldPos + sampleOffset * radius;

        // Project world-space sample to clip-space, then to UV
        vec4 clipPos = u_ViewProjection * vec4(sampleWorldPos, 1.0);
        clipPos.xyz /= clipPos.w;

        vec2 sampleUV;
        sampleUV.x = clipPos.x * 0.5 + 0.5;
        sampleUV.y = clipPos.y * -0.5 + 0.5;  // Vulkan Y-flip (matches deferred_lighting.frag)

        // Out-of-bounds → skip
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0) continue;

        // Read occluder world position from GBuffer
        vec3 occluderPos = texture(u_Position, sampleUV).xyz;

        // Occlusion test: camera-relative distance comparison
        float distSample   = distance(u_CameraPosition, sampleWorldPos);
        float distOccluder = distance(u_CameraPosition, occluderPos);

        float rangeCheck = smoothstep(0.0, 1.0, radius / distance(worldPos, occluderPos));

        // Occluder closer to camera than sample → sample is occluded
        if (distOccluder < distSample - bias)
            occlusion += 1.0 * rangeCheck;
    }

    float rawAO = 1.0 - occlusion / float(samples);
    o_AO = pow(rawAO, pc.u_Power);
}

#version 450 core
layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 1, binding = 0) uniform sampler2D u_DepthMap;
layout(set = 1, binding = 1) uniform sampler2D g_Albedo;
layout(set = 1, binding = 2) uniform sampler2D g_PBR;
layout(set = 1, binding = 4) uniform sampler2D g_Normal;
layout(set = 1, binding = 6) uniform sampler2D u_Lighting;
layout(set = 1, binding = 7) uniform sampler2D u_BlueNoise;
layout(set = 2, binding = 0) uniform sampler2D u_HiZ;

layout(push_constant) uniform PC {
    mat4  u_InvProj;
    mat4  u_Proj;
    mat4  u_View;
    float u_MaxSteps;
    float u_StepSize;
    float u_Thickness;
    float u_EdgeFade;
    int   u_MaxBinarySteps;
    float u_RoughnessCutoff;
    int   u_Enabled;
    int   u_HiZMipCount;
    int   u_FrameIndex;
} pc;

vec3 OctDecode(vec2 f) {
    f = f * 2.0 - 1.0;
    vec3 n = vec3(f, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

vec3 ViewPosFromDepth(vec2 uv, float depth, mat4 invProj) {
    // Vulkan negative viewport Y-flip: v_TexCoord.y=0→bottom, NDC.y=1→bottom
    vec4 ndc = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    vec4 vs = invProj * ndc;
    return vs.xyz / vs.w;
}

void main() {
    float depth = texture(u_DepthMap, v_TexCoord).r;
    if (depth >= 1.0) discard;

    vec3 viewPos = ViewPosFromDepth(v_TexCoord, depth, pc.u_InvProj);
    vec3 N = OctDecode(texture(g_Normal, v_TexCoord).rg);
    N = normalize(mat3(pc.u_View) * N);

    float roughness = texture(g_PBR, v_TexCoord).g;  // GBuffer[2].g = roughness

    if (roughness > pc.u_RoughnessCutoff) discard;
    // No metallic cull. Fresnel alone controls reflection strength:
    // dielectrics (water/marble) get strong SSR at grazing angles (NdotV≈0 → fresnel≈1).
    // Alpha formula: clamp(edgeFade * max(fresnel*3, 0.1) * hitFound, 0, 1)

    vec3 V = normalize(-viewPos);

    // Blue Noise normal jitter — roughness-driven cone scattering (tangent-space)
    // Use only .rg (2D direction); .b is 0 in the blue-noise texture, and reading it
    // via .rgb * 2 - 1 would introduce a constant z=-1 bias that tilts the normal.
    // Temporal jitter: offset the noise sampling every frame so the temporal accumulation
    // can average out the jitter noise (a STATIC pattern can't be smoothed over time).
    float frameHash = fract(sin(float(pc.u_FrameIndex) * 12.9898) * 43758.5453);
    vec2  noiseOffset = vec2(frameHash, fract(frameHash * 7.0)) * 64.0;
    vec2  noiseUV = v_TexCoord * vec2(textureSize(u_DepthMap, 0)) / 64.0 + noiseOffset;
    vec2 jitter2D = texture(u_BlueNoise, noiseUV).rg * 2.0 - 1.0;  // (cos, sin) ∈ [-1,1]²

    // Orthonormal tangent basis from the normal (view space)
    vec3 helper = (abs(N.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(helper, N));
    vec3 B = cross(N, T);

    float coneAngle = roughness * 0.15;  // reduced jitter → less noise (temporal handles blur)
    vec3 N_jittered = normalize(N + (T * jitter2D.x + B * jitter2D.y) * coneAngle);
    vec3 R = reflect(-V, N_jittered);

    // ── View-space ray march with view-space thickness (UE5-style) ──
    // Parameterize the ray in view space, project each step EXACTLY (depth linear, UV exact).
    float maxDist = pc.u_MaxSteps * pc.u_StepSize;
    float stepVS  = maxDist / float(int(pc.u_MaxSteps));
    float thicknessVS = pc.u_Thickness * 0.1;  // view-space (world units) hit tolerance

    // Ray origin biased along normal by a MINIMAL amount (avoids self-intersection
    // without skipping steps — a large bias loses the contact point).
    vec3 rayOriginVS = viewPos + N * 0.02;

    float hitFound = 0.0;
    vec4 hitColor = vec4(0.0);
    vec3 prevRayVS  = rayOriginVS;
    float prevRayDepth  = 0.0;
    float prevSceneDepth = 0.0;
    bool prevValid   = false;

    for (int i = 1; i <= int(pc.u_MaxSteps); i++) {
        vec3 rayPosVS = rayOriginVS + R * stepVS * float(i);

        // Project current step to NDC (exact, negative-viewport Y-flip)
        vec4 clipPos = pc.u_Proj * vec4(rayPosVS, 1.0);
        vec3 ndcPos  = clipPos.xyz / clipPos.w;
        vec2 rayUV   = vec2(ndcPos.x * 0.5 + 0.5, 1.0 - (ndcPos.y * 0.5 + 0.5));

        if (any(lessThan(rayUV, vec2(0.0))) || any(greaterThan(rayUV, vec2(1.0)))) break;

        float rayZ   = ndcPos.z;  // exact NDC Z
        float sceneZ = textureLod(u_DepthMap, rayUV, 0).r;
        if (sceneZ >= 1.0) { prevRayVS = rayPosVS; prevRayDepth = -rayPosVS.z; prevValid = false; continue; }

        // View-space depths (linear, positive in front of camera)
        float rayDepth = -rayPosVS.z;
        // Reconstruct surface view-space depth from NDC Z: viewZ = m23 / (-ndcZ - m22)
        float sceneDepth = -(pc.u_Proj[3][2] / (-sceneZ - pc.u_Proj[2][2]));

        // Crossing test (view-space thickness, correct at all distances)
        bool crossed = prevValid && (prevRayDepth <= prevSceneDepth + thicknessVS) && (rayDepth > sceneDepth);

        if (crossed || rayDepth > sceneDepth + thicknessVS) {
            // View-space midpoint binary refinement (perspective-correct)
            vec3 fPos = prevRayVS, bPos = rayPosVS;
            for (int j = 0; j < int(pc.u_MaxBinarySteps); j++) {
                vec3 mPos = (fPos + bPos) * 0.5;
                vec4 mClip = pc.u_Proj * vec4(mPos, 1.0);
                vec3 mNDC  = mClip.xyz / mClip.w;
                vec2 mUV   = vec2(mNDC.x * 0.5 + 0.5, 1.0 - (mNDC.y * 0.5 + 0.5));
                float mSceneZ = textureLod(u_DepthMap, mUV, 0).r;
                if (mSceneZ >= 1.0 || mNDC.z > mSceneZ) {
                    bPos = mPos;
                } else {
                    fPos = mPos;
                }
            }
            vec4 fClip = pc.u_Proj * vec4(fPos, 1.0);
            vec3 fNDC  = fClip.xyz / fClip.w;
            vec2 fUV   = vec2(fNDC.x * 0.5 + 0.5, 1.0 - (fNDC.y * 0.5 + 0.5));
            hitColor = textureLod(u_Lighting, fUV, 0.0);  // 0.0 — Lighting FBO has no mip chain
            hitFound = 1.0;
            break;
        }

        prevRayVS = rayPosVS; prevRayDepth = rayDepth; prevSceneDepth = sceneDepth; prevValid = true;
    }

    float edgeFade = smoothstep(0.0, pc.u_EdgeFade, v_TexCoord.x)
                   * smoothstep(0.0, pc.u_EdgeFade, 1.0 - v_TexCoord.x)
                   * smoothstep(0.0, pc.u_EdgeFade, v_TexCoord.y)
                   * smoothstep(0.0, pc.u_EdgeFade, 1.0 - v_TexCoord.y);
    float fresnel = pow(1.0 - max(dot(V, N), 0.0), 5.0);
    // Boost SSR weight so SSR visibly replaces IBL cubemap at hit pixels.
    // Without boost, typical fresnel≈0.3 makes ssrWeight too low → SSR+IBL double-overlay.
    // Fresnel controls reflection strength — no metallic factor. Dielectrics (water, marble)
    // have strong specular at grazing angles (Fresnel effect) and deserve SSR coverage.
    float alpha = clamp(edgeFade * max(fresnel * 3.0, 0.1) * hitFound, 0.0, 1.0);

    FragColor = vec4(hitColor.rgb * alpha, alpha);  // premultiplied alpha
}

#version 450 core
layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 1, binding = 0) uniform sampler2D u_DepthMap;
layout(set = 1, binding = 1) uniform sampler2D g_Albedo;
layout(set = 1, binding = 2) uniform sampler2D g_PBR;
layout(set = 1, binding = 4) uniform sampler2D g_Normal;
layout(set = 1, binding = 6) uniform sampler2D u_Lighting;

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
    float _pad;
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
    float metallic  = texture(g_PBR, v_TexCoord).r;

    if (roughness > pc.u_RoughnessCutoff) discard;
    if (metallic < 0.02) discard;

    vec3 V = normalize(-viewPos);
    vec3 R = reflect(-V, N);

    // ── View-space ray march with NDC-Z binary refinement ──
    float maxDist = pc.u_MaxSteps * pc.u_StepSize;
    float stepVS  = maxDist / float(int(pc.u_MaxSteps));
    float ndcThick = pc.u_Thickness * 0.01;

    float hitFound = 0.0;
    vec4 hitColor = vec4(0.0);
    vec3 prevRayVS  = viewPos;
    float prevRayZ  = 0.0;
    float prevSceneZ = 0.0;
    bool prevValid   = false;

    // Project starting point for crossing test
    {
        vec4 c0 = pc.u_Proj * vec4(viewPos, 1.0);
        prevRayZ = (c0.z / c0.w);
    }

    for (int i = 1; i <= int(pc.u_MaxSteps); i++) {
        vec3 rayPosVS = viewPos + R * stepVS * float(i);
        // Skip first 2 steps to avoid self-intersection
        if (i <= 2) {
            prevRayVS = rayPosVS;
            vec4 cp = pc.u_Proj * vec4(rayPosVS, 1.0);
            prevRayZ = (cp.z / cp.w);
            prevValid = false;
            continue;
        }

        // Project current step to NDC
        vec4 clipPos = pc.u_Proj * vec4(rayPosVS, 1.0);
        vec3 ndcPos  = clipPos.xyz / clipPos.w;
        vec2 rayUV   = vec2(ndcPos.x * 0.5 + 0.5, 1.0 - (ndcPos.y * 0.5 + 0.5));

        if (any(lessThan(rayUV, vec2(0.0))) || any(greaterThan(rayUV, vec2(1.0)))) break;

        float rayZ   = ndcPos.z;
        float sceneZ = textureLod(u_DepthMap, rayUV, 0).r;
        if (sceneZ >= 1.0) { prevRayVS = rayPosVS; prevRayZ = rayZ; prevValid = false; continue; }

        // Crossing test: only trigger when ray went from front→behind
        // Prevents false hits at depth discontinuities (vertical smearing).
        bool crossed = prevValid && (prevRayZ <= prevSceneZ + ndcThick) && (rayZ > sceneZ);

        if (crossed || rayZ > sceneZ + ndcThick) {
            // All hits use binary refinement for consistent precision (no banding).
            // NDC-Z space midpoint: perspective-correct for uniform convergence (no wobbly edges).
            float fZ = 0.0, bZ = 0.0;
            {
                vec4 fc = pc.u_Proj * vec4(prevRayVS, 1.0);
                vec4 bc = pc.u_Proj * vec4(rayPosVS, 1.0);
                fZ = (fc.z / fc.w); bZ = (bc.z / bc.w);
            }
            vec3 fPos = prevRayVS, bPos = rayPosVS;
            for (int j = 0; j < int(pc.u_MaxBinarySteps); j++) {
                // Midpoint in NDC Z — NOT view-space midpoint (perspective-correct)
                float mZ = (fZ + bZ) * 0.5;
                float t  = (mZ - fZ) / max(bZ - fZ, 1e-6);
                vec3 mPos = mix(fPos, bPos, clamp(t, 0.001, 0.999));

                vec4 mClip = pc.u_Proj * vec4(mPos, 1.0);
                vec3 mNDC  = mClip.xyz / mClip.w;
                vec2 mUV   = vec2(mNDC.x * 0.5 + 0.5, 1.0 - (mNDC.y * 0.5 + 0.5));
                float mSceneZ = textureLod(u_DepthMap, mUV, 0).r;
                if (mSceneZ >= 1.0 || mNDC.z > mSceneZ) {
                    bZ = mZ; bPos = mPos;
                } else {
                    fZ = mZ; fPos = mPos;
                }
            }
            vec4 fClip = pc.u_Proj * vec4(fPos, 1.0);
            vec3 fNDC  = fClip.xyz / fClip.w;
            vec2 fUV   = vec2(fNDC.x * 0.5 + 0.5, 1.0 - (fNDC.y * 0.5 + 0.5));
            hitColor = textureLod(u_Lighting, fUV, roughness * 4.0);
            hitFound = 1.0;
            break;
        }

        prevRayVS  = rayPosVS;
        prevRayZ   = rayZ;
        prevSceneZ = sceneZ;
        prevValid  = true;
    }

    float edgeFade = smoothstep(0.0, pc.u_EdgeFade, v_TexCoord.x)
                   * smoothstep(0.0, pc.u_EdgeFade, 1.0 - v_TexCoord.x)
                   * smoothstep(0.0, pc.u_EdgeFade, v_TexCoord.y)
                   * smoothstep(0.0, pc.u_EdgeFade, 1.0 - v_TexCoord.y);
    float fresnel = pow(1.0 - max(dot(V, N), 0.0), 5.0);
    // Boost SSR weight so SSR visibly replaces IBL cubemap at hit pixels.
    // Without boost, typical fresnel≈0.3 makes ssrWeight too low → SSR+IBL double-overlay.
    float alpha = clamp(edgeFade * max(fresnel * 3.0, 0.1) * metallic * hitFound, 0.0, 1.0);

    FragColor = vec4(hitColor.rgb, alpha);
}

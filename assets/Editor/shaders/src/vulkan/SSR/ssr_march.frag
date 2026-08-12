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
    float _pad2;
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

    // Blue Noise normal jitter — roughness-driven cone scattering
    vec2 noiseUV = v_TexCoord * vec2(textureSize(u_DepthMap, 0)) / 64.0;
    vec3 jitter  = texture(u_BlueNoise, noiseUV).rgb * 2.0 - 1.0;
    float coneAngle = roughness * 0.3;
    vec3 N_jittered = normalize(N + jitter * coneAngle);
    vec3 R = reflect(-V, N_jittered);

    // ── Hi-Z accelerated ray march ──
    // Traverses the depth pyramid in screen space, jumping over empty cells.
    // Falls back to NDC-Z binary refinement at mip 0 for pixel-accurate intersection.
    // When HiZMipCount <= 0, falls back to linear screen-space stepping.
    float maxDist = pc.u_MaxSteps * pc.u_StepSize;

    // Project ray start and end to screen UV
    vec3 rayEndVS = viewPos + R * maxDist;
    vec4 cs = pc.u_Proj * vec4(viewPos, 1.0);
    vec4 ce = pc.u_Proj * vec4(rayEndVS, 1.0);
    float zStart = cs.z / cs.w;
    float zEnd   = ce.z / ce.w;
    vec2 uvStart = vec2(cs.x / cs.w * 0.5 + 0.5, 1.0 - (cs.y / cs.w * 0.5 + 0.5));
    vec2 uvEnd   = vec2(ce.x / ce.w * 0.5 + 0.5, 1.0 - (ce.y / ce.w * 0.5 + 0.5));
    vec2 ssDir   = uvEnd - uvStart;
    float ssDist = length(ssDir);
    if (ssDist < 0.0001) discard;
    ssDir /= ssDist;

    float hitFound = 0.0;
    vec4 hitColor = vec4(0.0);

    if (pc.u_HiZMipCount > 0) {
    // ═══════════════════════════════════════════════════════════════
    // Hi-Z DDA traversal — logarithmic search through depth pyramid
    // ═══════════════════════════════════════════════════════════════
    float mipLevel = max(float(pc.u_HiZMipCount) - 1.0, 0.0);
    vec2 uv = uvStart;
    vec2 prevUV = uvStart;

    int maxIters = int(pc.u_MaxSteps) * 2;
    for (int iter = 0; iter < maxIters; iter++) {
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) break;

        int mip = int(mipLevel);
        float cellSize = exp2(float(mip)) / float(textureSize(u_HiZ, mip).x);

        // Current Hi-Z cell bounds
        vec2 cellMin = floor(uv / cellSize) * cellSize;
        vec2 sampleUV = clamp(cellMin + cellSize * 0.5, 0.0, 1.0);

        // Conservative min-depth from Hi-Z (MAX-reduced: furthest depth in this cell)
        float minDepth = textureLod(u_HiZ, sampleUV, float(mip)).r;

        // Ray Z at current UV — linear NDC Z interpolation (standard approximation).
        float t = distance(uvStart, uv) / max(ssDist, 1e-6);
        float rayZ = mix(zStart, zEnd, clamp(t, 0.0, 1.0));

        if (rayZ > minDepth) {
            // Ray is behind everything in this cell → DDA cell crossing (robust vs AABB)
            vec2 crossStep = sign(ssDir);
            vec2 crossUV   = cellMin + max(crossStep, vec2(0.0)) * cellSize;
            vec2 tCross    = (crossUV - uv) / max(abs(ssDir), 1e-6);
            float tExit    = min(tCross.x, tCross.y) + 0.0001;
            prevUV = uv;
            uv += ssDir * tExit;
            mipLevel = min(mipLevel + 1.0, float(pc.u_HiZMipCount) - 1.0);
        } else {
            // Ray may intersect geometry in this cell
            if (mip <= 0) {
                // At mip 0: check actual depth and binary-refine
                float sceneZ = textureLod(u_DepthMap, uv, 0).r;
                if (sceneZ < 1.0 && rayZ > sceneZ && rayZ < sceneZ + pc.u_Thickness) {
                    // Binary refinement between prevUV (front) and uv (back)
                    vec3 prevVS = viewPos + R * (maxDist * distance(uvStart, prevUV) / max(ssDist, 1e-6));
                    vec3 currVS = viewPos + R * (maxDist * distance(uvStart, uv) / max(ssDist, 1e-6));
                    float fZ = 0.0, bZ = 0.0;
                    {
                        vec4 fc = pc.u_Proj * vec4(prevVS, 1.0);
                        vec4 bc = pc.u_Proj * vec4(currVS, 1.0);
                        fZ = fc.z / fc.w; bZ = bc.z / bc.w;
                    }
                    vec3 fPos = prevVS, bPos = currVS;
                    for (int j = 0; j < int(pc.u_MaxBinarySteps); j++) {
                        float mZ = (fZ + bZ) * 0.5;
                        float frac = (mZ - fZ) / max(bZ - fZ, 1e-6);
                        vec3 mPos = mix(fPos, bPos, clamp(frac, 0.001, 0.999));
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
                    hitColor = textureLod(u_Lighting, fUV, 0.0);
                    hitFound = 1.0;
                    break;
                }
                // No hit at mip 0, advance one pixel
                prevUV = uv;
                uv += ssDir * cellSize;
            } else {
                // Descend one mip level — reset prevUV so binary refinement has a tight search range.
                prevUV = uv;
                mipLevel -= 1.0;
            }
        }
    }
    } else {
    // ═══════════════════════════════════════════════════════════════
    // Linear screen-space ray march — no Hi-Z acceleration
    // ═══════════════════════════════════════════════════════════════
    for (int i = 0; i < int(pc.u_MaxSteps); i++) {
        float t = (float(i) + 0.5) / float(pc.u_MaxSteps);
        vec2 uv_i = mix(uvStart, uvEnd, t);

        if (any(lessThan(uv_i, vec2(0.0))) || any(greaterThan(uv_i, vec2(1.0)))) break;

        float rayZ = mix(zStart, zEnd, t);
        float sceneZ = textureLod(u_DepthMap, uv_i, 0).r;
        if (sceneZ >= 1.0) continue;

        // Cross-check: ray crosses from in-front to behind the surface
        if (i > 0) {
            float tPrev = (float(i) - 0.5) / float(pc.u_MaxSteps);
            float prevRayZ = mix(zStart, zEnd, tPrev);
            if (prevRayZ > sceneZ + pc.u_Thickness) continue; // already behind at prev step → skip
        }

        if (rayZ > sceneZ && rayZ < sceneZ + pc.u_Thickness) {
            hitColor = textureLod(u_Lighting, uv_i, 0.0);
            hitFound = 1.0;
            break;
        }
    }
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

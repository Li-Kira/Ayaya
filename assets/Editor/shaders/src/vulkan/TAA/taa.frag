#version 450 core

// ── Temporal Anti-Aliasing (UE5-style) ──
// Runs in HDR linear space, before tone mapping, on the composed "Lighting" buffer.
// Jitter is baked into the per-object velocity (gbuffer_gdr.vert uses the jittered
// u_ViewProjection / u_PrevViewProjection), so reprojection needs no explicit jitter
// compensation. Camera-only motion fallback covers pixels without object velocity
// (skybox / editor grid / sprites).

layout(set = 1, binding = 0) uniform sampler2D u_SceneColor;   // Lighting HDR (RGBA16F)
layout(set = 1, binding = 1) uniform sampler2D u_Velocity;     // GBuffer attach 4 (RG16F, currentUV-prevUV)
layout(set = 1, binding = 2) uniform sampler2D u_DepthMap;     // SceneDepth (current frame)
layout(set = 1, binding = 3) uniform sampler2D g_Normal;       // GBuffer attach 0 (oct-encoded world normal)
layout(set = 1, binding = 4) uniform sampler2D u_DepthHistory; // SceneDepth (previous frame)
layout(set = 1, binding = 5) uniform sampler2D u_History;      // previous frame TAA_Output (RGBA16F)

layout(push_constant) uniform PC {
    mat4  u_InvViewProjection;   // current frame (for camera-only motion)
    mat4  u_PrevViewProjection;  // previous frame
    vec2  u_Jitter;              // current frame sub-pixel jitter (UV units, informational)
    vec2  u_TexelSize;           // 1/w, 1/h
    float u_BlendFactor;         // base current-frame weight (≈0.05 → 95% history)
    float u_DepthThreshold;      // disocclusion depth diff threshold
    float u_NormalThreshold;     // disocclusion normal dot threshold
    float u_MotionThreshold;     // large-motion history rejection gate
    float u_SharpenAmount;       // adaptive sharpen strength (0 = off)
    float u_Enable;              // passthrough when 0
} pc;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

// Octahedral normal decode (matches gbuffer / SSR temporal).
vec3 OctDecode(vec2 f) {
    f = f * 2.0 - 1.0;
    vec3 n = vec3(f, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

// YCoCg — tighter AABB bounds than RGB for neighborhood clamping (Frostbite-style).
vec3 RGBToYCoCg(vec3 c) {
    float co  = c.x - c.z;
    float tmp = c.z + co * 0.5;
    float cg  = c.y - tmp;
    float y   = tmp + cg * 0.5;
    return vec3(y, co, cg);
}
vec3 YCoCgToRGB(vec3 ycocg) {
    float tmp = ycocg.x - ycocg.y * 0.5;
    float g   = ycocg.y + tmp;
    float b   = tmp - ycocg.z * 0.5;
    float r   = b + ycocg.z;
    return vec3(r, g, b);
}

// Screen-space camera-only motion for a static world point, reconstructed from
// depth + current/previous VP. Matches gbuffer_gdr.vert's UV convention (Y-flip).
vec2 ComputeCameraMotion(vec2 uv, float depth) {
    vec2 ndcXY = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    vec4 h = pc.u_InvViewProjection * vec4(ndcXY, depth, 1.0);
    vec3 worldPos = h.xyz / h.w;

    vec4 prevClip = pc.u_PrevViewProjection * vec4(worldPos, 1.0);
    vec2 prevNDC  = prevClip.xy / prevClip.w;
    vec2 prevUV   = vec2(prevNDC.x * 0.5 + 0.5, 1.0 - (prevNDC.y * 0.5 + 0.5));
    return uv - prevUV;
}

void main() {
    vec4 current = texture(u_SceneColor, v_TexCoord);

    if (pc.u_Enable < 0.5) {
        FragColor = current;
        return;
    }

    // ── 1. Motion vector: object velocity, fall back to camera-only for pixels
    //    without object velocity (skybox / grid / sprites have velocity == 0).
    float currentDepth = texture(u_DepthMap, v_TexCoord).r;
    vec2  objMotion    = texture(u_Velocity, v_TexCoord).rg;
    vec2  camMotion    = ComputeCameraMotion(v_TexCoord, currentDepth);
    vec2  motion       = (length(objMotion) < 1e-3) ? camMotion : objMotion;
    vec2  historyUV    = v_TexCoord - motion;

    // ── 2. History sample + neighborhood clamp (anti-ghosting).
    vec3 currentYC = RGBToYCoCg(current.rgb);
    vec3 neighMin  = currentYC;
    vec3 neighMax  = currentYC;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            vec3 c = RGBToYCoCg(
                texture(u_SceneColor, v_TexCoord + vec2(float(dx), float(dy)) * pc.u_TexelSize).rgb);
            neighMin = min(neighMin, c);
            neighMax = max(neighMax, c);
        }
    }
    // 10% variance boost — tolerate more history variance (less ghosting).
    vec3 mean  = (neighMin + neighMax) * 0.5;
    vec3 delta = (neighMax - neighMin) * 0.5;
    neighMin   = mean - delta * 1.1;
    neighMax   = mean + delta * 1.1;

    vec4 history = texture(u_History, historyUV);
    vec3 historyYC = clamp(RGBToYCoCg(history.rgb), neighMin, neighMax);
    history.rgb = YCoCgToRGB(historyYC);

    // ── 3. Disocclusion rejection — previous-frame depth + normal divergence.
    float historyDepth = texture(u_DepthHistory, historyUV).r;
    float depthDiff    = abs(currentDepth - historyDepth);

    vec3 currentNormal = OctDecode(texture(g_Normal, v_TexCoord).rg);
    vec3 historyNormal = OctDecode(texture(g_Normal, historyUV).rg);
    float normalAgree  = dot(currentNormal, historyNormal);

    bool disoccluded = (currentDepth >= 1.0 || historyDepth >= 1.0)  // sky vs. geometry
                    || depthDiff > pc.u_DepthThreshold
                    || normalAgree < pc.u_NormalThreshold;

    // ── 4. Adaptive blend: reject history on disocclusion / large motion.
    float blendFactor = pc.u_BlendFactor;
    if (disoccluded) blendFactor = 1.0;
    float motionMag = length(motion);
    if (motionMag > pc.u_MotionThreshold)
        blendFactor = clamp(blendFactor + (motionMag - pc.u_MotionThreshold) * 4.0, blendFactor, 0.8);

    vec3 resolved = mix(history.rgb, current.rgb, blendFactor);

    // ── 5. Adaptive sharpen (unsharp mask on the clamped history, already in
    //    history space — counteracts temporal blur).
    if (pc.u_SharpenAmount > 0.0) {
        vec2 ts = pc.u_TexelSize;
        vec3 hL = YCoCgToRGB(clamp(RGBToYCoCg(texture(u_History, historyUV + vec2(-ts.x, 0.0)).rgb), neighMin, neighMax));
        vec3 hR = YCoCgToRGB(clamp(RGBToYCoCg(texture(u_History, historyUV + vec2( ts.x, 0.0)).rgb), neighMin, neighMax));
        vec3 hU = YCoCgToRGB(clamp(RGBToYCoCg(texture(u_History, historyUV + vec2(0.0, -ts.y)).rgb), neighMin, neighMax));
        vec3 hD = YCoCgToRGB(clamp(RGBToYCoCg(texture(u_History, historyUV + vec2(0.0,  ts.y)).rgb), neighMin, neighMax));
        vec3 blur = (hL + hR + hU + hD) * 0.25;
        resolved += (resolved - blur) * pc.u_SharpenAmount;
    }

    FragColor = vec4(resolved, mix(history.a, current.a, blendFactor));
}

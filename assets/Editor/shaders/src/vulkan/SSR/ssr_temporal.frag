#version 450 core

layout(set = 1, binding = 0) uniform sampler2D u_SSRBlurred;   // current frame SSR (spatial clamp source)
layout(set = 1, binding = 1) uniform sampler2D u_Velocity;   // GBuffer attachment 4 (per-object motion)
layout(set = 1, binding = 2) uniform sampler2D u_DepthMap;
layout(set = 1, binding = 3) uniform sampler2D g_Normal;   // RGB = world normal (oct-encoded)
layout(set = 1, binding = 4) uniform sampler2D g_PBR;       // G = roughness
layout(set = 1, binding = 5) uniform sampler2D u_SSRHistory; // previous frame temporal result
layout(set = 1, binding = 6) uniform sampler2D u_DepthHistory; // previous frame depth (true disocclusion)

layout(push_constant) uniform PC {
    float u_DepthThreshold;
    float u_NormalThreshold;
    float u_TemporalBlend;
    float u_HasHistory;     // 1.0 = valid history; 0.0 = first frame (passthrough)
    vec2  u_TexelSize;      // 1/w, 1/h for SSR_Blurred (½res)
    vec2  _pad2;
} pc;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

vec3 OctDecode(vec2 f) {
    f = f * 2.0 - 1.0;
    vec3 n = vec3(f, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

void main() {
    // 1. 当前帧 SSR
    vec4 currentSSR = texture(u_SSRBlurred, v_TexCoord);

    // First frame (no valid history): output the current SSR directly. The C++ pass
    // still binds the current frame's SSR_Blurred as u_SSRHistory, but reprojecting
    // that fallback by the velocity would sample it at the WRONG position and smear
    // the reflection across the first ~20 frames (the temporal blend is slow).
    if (pc.u_HasHistory < 0.5) {
        FragColor = currentSSR;
        return;
    }

    // 2. Motion vector — normalized UV is resolution-independent:
    //    full-res velocity is sampled at the SAME v_TexCoord (NOT *2.0).
    vec2 motion = texture(u_Velocity, v_TexCoord).rg;

    // 3. Reproject history — velocity = currentUV - prevUV, so the previous
    //    frame position is currentUV - velocity (NOT +). Adding would sample the
    //    history in the WRONG direction (double the displacement).
    vec2 historyUV = v_TexCoord - motion;

    // 4. 遮挡检测 — 双因素 (深度 + 法线)
    // currentDepth 采样当前帧深度，historyDepth 必须采样【上一帧】深度：
    // disocclusion 的本质是「上一帧 historyUV 处是另一个更近的表面（遮挡物）」，
    // 若用当前帧深度，遮挡物已经移开、差异被抹平，快速相机运动下会 ghosting。
    float currentDepth = texture(u_DepthMap, v_TexCoord).r;
    float historyDepth = texture(u_DepthHistory, historyUV).r;

    float depthDiff = abs(currentDepth - historyDepth);

    vec3 currentNormal = OctDecode(texture(g_Normal, v_TexCoord).rg);
    vec3 historyNormal = OctDecode(texture(g_Normal, historyUV).rg);
    float normalAgree = dot(currentNormal, historyNormal);

    bool disoccluded = (currentDepth >= 1.0 || historyDepth >= 1.0)  // sky vs. geometry
                    || depthDiff > pc.u_DepthThreshold
                    || normalAgree < pc.u_NormalThreshold;

    // 5. Neighborhood Clamp (防 Ghosting)
    // SSR is premultiplied alpha (rgb = hitColor * alpha). Clamping rgb alone while
    // leaving alpha free creates "black + opaque" pixels (rgb=0, alpha>0) at silhouettes
    // where the current neighborhood is all-miss — these dim the IBL in ApplyReflection
    // and appear as black rectangles. Clamp alpha with the same neighborhood so a miss
    // neighborhood forces the pixel back to transparent instead of black+opaque.
    vec3  neighMin  = currentSSR.rgb;
    vec3  neighMax  = currentSSR.rgb;
    float neighMinA = currentSSR.a;
    float neighMaxA = currentSSR.a;
    vec2 ts = pc.u_TexelSize;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            vec2 neighUV = v_TexCoord + vec2(float(dx), float(dy)) * ts;
            vec4 c = texture(u_SSRBlurred, neighUV);
            neighMin  = min(neighMin,  c.rgb);
            neighMax  = max(neighMax,  c.rgb);
            neighMinA = min(neighMinA, c.a);
            neighMaxA = max(neighMaxA, c.a);
        }
    }
    // variance boost — 10% expansion for tolerance (tighter = less ghosting)
    vec3 mean  = (neighMin + neighMax) * 0.5;
    vec3 delta = (neighMax - neighMin) * 0.5;
    neighMin = mean - delta * 1.1;
    neighMax = mean + delta * 1.1;

    vec4 historyColor = texture(u_SSRHistory, historyUV);
    historyColor.rgb = clamp(historyColor.rgb, neighMin, neighMax);
    historyColor.a   = clamp(historyColor.a,   neighMinA,  neighMaxA);

    // 6. 混合权重 (Roughness 自适应 + velocity 因子)
    float roughness = texture(g_PBR, v_TexCoord).g;
    float motionMag = length(motion);

    float blendFactor;
    if (disoccluded) {
        blendFactor = 1.0;  // 完全丢弃历史
    } else {
        // roughness 低 (镜面) → 高当前权重 → 保持锐利
        // roughness 高 (粗糙) → 高历史权重 → 强力去噪
        blendFactor = mix(0.30, pc.u_TemporalBlend, smoothstep(0.1, 0.6, roughness));
        // 快速移动 → 历史不可靠 → 提高当前权重（减少拖影）
        blendFactor = min(blendFactor + motionMag * 2.0, 0.8);
    }

    FragColor.rgb = mix(historyColor.rgb, currentSSR.rgb, blendFactor);
    FragColor.a   = mix(historyColor.a,   currentSSR.a,   blendFactor);
}

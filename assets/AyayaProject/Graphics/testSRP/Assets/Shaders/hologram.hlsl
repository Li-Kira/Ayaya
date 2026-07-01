// hologram.hlsl — GDR-compatible transparent hologram effect
// TA 零声明 — all constants from AyayaGDR.hlsl.

#include "Generic/AyayaGDR.hlsl"

struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float2 uv       : TEXCOORD1;
    nointerpolation uint matIdx : TEXCOORD2;
};

#ifdef VERTEX_SHADER
VSOutput main(uint vID : SV_VertexID, uint iID : SV_InstanceID) {
    AyayaVertex v = GetAyayaVertex(vID, iID);
    VSOutput o;
    float4 wPos = mul(v.worldMatrix, float4(v.position, 1.0));
    o.position = mul(viewProj, wPos);
    o.worldPos = wPos.xyz;
    o.uv       = v.uv;
    o.matIdx   = v.materialIdx;
    return o;
}
#else
float4 main(VSOutput i) : SV_TARGET {
    GPUMaterial mat = u_Materials[i.matIdx];

    // Hologram base color from material customData[0], fallback cyan
    float4 holoColor = mat.customData[0];
    if (dot(holoColor.rgb, holoColor.rgb) < 0.001)
        holoColor = float4(0.0, 0.8, 1.0, 0.6);

    // Scan speed from material customData[1].x, fallback 1.5
    float scanSpeed = max(mat.customData[1].x, 0.5);

    // ── Tier 1: Primary scanlines (thick, slow, moving upward) ──
    float scan1 = frac(i.worldPos.y * 4.0 + _Time.y * scanSpeed * 0.6);
    float line1 = 1.0 - abs(scan1 - 0.5) * 12.0; // glow strip
    line1 = saturate(line1);

    // ── Tier 2: Secondary scanlines (thin, fast, moving downward) ──
    float scan2 = frac(i.worldPos.y * 8.0 - _Time.y * scanSpeed * 1.2);
    float line2 = 1.0 - abs(scan2 - 0.5) * 20.0;
    line2 = saturate(line2) * 0.4;

    // ── Tier 3: Horizontal interference (glitch) ──
    float glitch = step(0.98, frac(i.worldPos.y * 2.5 + _Time.y * 0.3));
    float line3 = glitch * 0.3;

    // ── Fresnel edge glow ──
    float3 viewDir = normalize(cameraPos - i.worldPos);
    float3 N = normalize(cross(ddx(i.worldPos), ddy(i.worldPos)));
    float fresnel = 1.0 - abs(dot(N, viewDir));
    fresnel = pow(fresnel, 3.0) * 0.5;

    // ── Composite ──
    float alpha = saturate(line1 + line2 + line3 + fresnel) * holoColor.a;
    float3 color = holoColor.rgb * (1.0 + line1 * 0.5); // glow boost on scan peaks

    return float4(color * alpha, alpha);
}
#endif

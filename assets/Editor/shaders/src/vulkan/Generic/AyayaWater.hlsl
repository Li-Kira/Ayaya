// AyayaWater.hlsl — water rendering helpers (Gerstner waves + fresnel).
// Include AFTER "Generic/AyayaGDR.hlsl". Y-up world, XZ horizontal water plane.

#define AYAYA_WATER_PI 3.14159265359

// Gerstner wave. Returns position displacement; accumulates the tangent/binormal
// perturbation used for the analytic normal (normalize(cross(binormal, tangent))).
// dir = normalized XZ direction, steepness ∈ [0,1], wavelength, amplitude, speed.
float3 GerstnerWave(float2 dir, float steepness, float wavelength, float amplitude, float speed,
                    float2 posXZ, float time, inout float3 tangent, inout float3 binormal) {
    float k = 2.0 * AYAYA_WATER_PI / max(wavelength, 0.001);
    float2 d = normalize(dir + 1e-5);
    float f = k * (dot(d, posXZ) - speed * time);
    float a = steepness / k;

    tangent  += float3(-d.x * d.x * steepness * sin(f), d.x * steepness * cos(f), -d.x * d.y * steepness * sin(f));
    binormal += float3(-d.x * d.y * steepness * sin(f), d.y * steepness * cos(f), -d.y * d.y * steepness * sin(f));

    return float3(d.x * a * cos(f), amplitude * sin(f), d.y * a * cos(f));
}

// Schlick fresnel (F0 approximation). cosTheta = dot(N, V).
float WaterFresnel(float cosTheta, float f0) {
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Reconstruct LINEAR view depth (meters) from Vulkan [0,1] NDC depth.
// Uses the projection matrix's z coefficients (CameraUBO.projection).
// NOTE: DXC compiles cbuffer matrices as row_major (transposed vs GLM's column-major).
// The perspective matrix is: row2 = [0,0,zz,zw], row3 = [0,0,-1,0], so in row-major
// indexing the zw coefficient is projection[3][2] (NOT [2][3]).
float LinearizeDepth(float ndc) {
    float zz = projection[2][2];   // far / (near - far)
    float zw = projection[3][2];   // near * far / (near - far)
    float n  = zw / zz;            // near plane
    float f  = zw / (zz + 1.0);    // far plane
    return n * f / max(f - ndc * (f - n), 1e-5);
}

// Procedural detail ripple normal. Four slowly-drifting sine waves with incommensurate
// frequencies/directions (so the pattern doesn't visibly tile) and an ANALYTIC gradient
// (so the normal stays smooth — a finite-difference of value noise looks jagged). Returns
// a world-space (Y-up) normal with a small tilt for subtle pool ripples.
float3 RippleDetailNormal(float2 worldXZ, float time) {
    float2 d0 = normalize(float2(1.0, 0.3));
    float2 d1 = normalize(float2(-0.4, 1.0));
    float2 d2 = normalize(float2(0.7, -0.7));
    float2 d3 = normalize(float2(-0.2, -0.9));

    float k0 = 5.0,  k1 = 7.0,  k2 = 11.0, k3 = 13.0;   // incommensurate → non-tiling
    float s0 = 1.5,  s1 = 1.1,  s2 = 0.9,  s3 = 0.7;    // slow drift speeds

    float w0 = dot(d0, worldXZ) * k0 + time * s0;
    float w1 = dot(d1, worldXZ) * k1 + time * s1;
    float w2 = dot(d2, worldXZ) * k2 + time * s2;
    float w3 = dot(d3, worldXZ) * k3 + time * s3;

    // h = A * (sin(w0) + 0.7 sin(w1) + 0.5 sin(w2) + 0.3 sin(w3)); analytic gradient:
    float A = 0.03;
    float dhx = A * (cos(w0) * k0 * d0.x + 0.7 * cos(w1) * k1 * d1.x
                   + 0.5 * cos(w2) * k2 * d2.x + 0.3 * cos(w3) * k3 * d3.x);
    float dhz = A * (cos(w0) * k0 * d0.y + 0.7 * cos(w1) * k1 * d1.y
                   + 0.5 * cos(w2) * k2 * d2.y + 0.3 * cos(w3) * k3 * d3.y);

    return normalize(float3(-dhx, 1.0, -dhz));
}

// Procedural pool caustics — derived from the water surface CURVATURE (the Laplacian of
// the wave height field), so the pattern is physically tied to the same waves that drive
// the surface and moves/shimmer with them. Concave (focusing) regions → bright lines.
// Uses the material's 2 Gerstner waves (large scale) + the ripple's 4 waves (fine detail).
float3 PoolCaustics(float2 worldXZ, float time, float4 w0, float4 w1, float4 w2) {
    // Gerstner waves (large-scale focusing) — same params as the vertex shader:
    //   wave0: dir=normalize(w1.xy), wavelength=w0.y, amplitude=w0.x, speed=w0.w
    //   wave1: dir=normalize(w2.zw), wavelength=w1.w, amplitude=w1.z, speed=w2.y
    float2 gd0 = normalize(w1.xy + 1e-5);
    float2 gd1 = normalize(w2.zw + 1e-5);
    float gk0 = 2.0 * AYAYA_WATER_PI / max(w0.y, 0.001);
    float gk1 = 2.0 * AYAYA_WATER_PI / max(w1.w, 0.001);
    float c = w0.x * gk0 * gk0 * sin(gk0 * (dot(gd0, worldXZ) - w0.w * time))
            + w1.z * gk1 * gk1 * sin(gk1 * (dot(gd1, worldXZ) - w2.y * time));

    // Ripple waves (fine caustic detail) — same directions/frequencies as RippleDetailNormal.
    // Amplitude 0.002 (not 0.03): the caustic uses curvature (A·k²), which is far larger
    // than the normal's slope (A·k), so it needs a much smaller A to stay balanced.
    float2 d0 = normalize(float2(1.0, 0.3));
    float2 d1 = normalize(float2(-0.4, 1.0));
    float2 d2 = normalize(float2(0.7, -0.7));
    float2 d3 = normalize(float2(-0.2, -0.9));
    c += 0.002 * (25.0   * sin(5.0  * dot(d0, worldXZ) + 1.5 * time)
                + 34.3   * sin(7.0  * dot(d1, worldXZ) + 1.1 * time)
                + 60.5   * sin(11.0 * dot(d2, worldXZ) + 0.9 * time)
                + 50.7   * sin(13.0 * dot(d3, worldXZ) + 0.7 * time));

    c = saturate(c * 2.0);   // keep the focusing (concave) part only
    c = c * c;               // sharpen into lines
    return float3(c, c, c);
}

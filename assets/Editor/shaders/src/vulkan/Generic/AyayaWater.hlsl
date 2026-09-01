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

#version 410 core

out vec4 FragColor;
in vec3 v_LocalPos;

uniform samplerCube u_EnvironmentMap;
uniform float u_Roughness;

const float PI = 3.14159265359;

// 【终极方案】：使用 OpenGL 4.0 原生硬件指令 bitfieldReverse！既快又绝对不会出 Bug！
vec2 Hammersley(uint i, uint N) {
    return vec2(float(i)/float(N), float(bitfieldReverse(i)) * 2.3283064365386963e-10);
}

vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness*roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

void main() {		
    vec3 N = normalize(v_LocalPos);
    vec3 R = N; vec3 V = R;

    // 【防死黑补丁】：限制最小 roughness 为 0.001，防止除以 0 产生 NaN 导致全屏死黑！
    float safeRoughness = max(u_Roughness, 0.001);

    const uint SAMPLE_COUNT = 1024u;
    vec3 prefilteredColor = vec3(0.0);
    float totalWeight = 0.0;
    
    for(uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        // 注意这里全部使用 safeRoughness
        vec3 H = ImportanceSampleGGX(Xi, N, safeRoughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if(NdotL > 0.0) {
            float D = DistributionGGX(N, H, safeRoughness);
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float pdf = D * NdotH / (4.0 * HdotV) + 0.0001; 

            float resolution = 1024.0; 
            float saTexel  = 4.0 * PI / (6.0 * resolution * resolution);
            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);
            float mipLevel = u_Roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel); 
            
            prefilteredColor += textureLod(u_EnvironmentMap, L, mipLevel).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    FragColor = vec4(prefilteredColor / totalWeight, 1.0);
}
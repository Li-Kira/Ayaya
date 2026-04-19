#version 450 core
layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec3 v_LocalPos;

// 【修正】：在 Vulkan 中必须指定 Descriptor Set (通常贴图放在 Set 0 或 1)
layout(set = 1, binding = 0) uniform sampler2D u_EquirectangularMap;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main() {       
    vec2 uv = SampleSphericalMap(normalize(v_LocalPos));
    vec3 color = texture(u_EquirectangularMap, uv).rgb;
    
    // 直接输出 HDR 颜色，不要在这里做 ToneMapping
    FragColor = vec4(color, 1.0);
}
#version 450 core
layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec3 v_LocalPos;

// 【修复】：必须显式声明 set = 1
layout(set = 1, binding = 0) uniform samplerCube u_EnvironmentMap;

const float PI = 3.14159265359;

void main() {       
    vec3 N = normalize(v_LocalPos);
    vec3 irradiance = vec3(0.0);   
    
    vec3 up = vec3(0.0, 1.0, 0.0);
    if (abs(N.y) > 0.999) {
        up = vec3(0.0, 0.0, 1.0);
    }
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));
       
    float sampleDelta = 0.05;  // ~728 samples (was 0.1, ~182)
    float nrSamples = 0.0;
    
    for(float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for(float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            vec3 tangentSample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N; 
            
            irradiance += texture(u_EnvironmentMap, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * irradiance * (1.0 / float(nrSamples));
    
    FragColor = vec4(irradiance, 1.0);
}
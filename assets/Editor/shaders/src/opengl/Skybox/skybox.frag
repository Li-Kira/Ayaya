#version 410 core
out vec4 FragColor;

in vec3 v_TexCoords;

uniform samplerCube u_Skybox; 
uniform float u_Intensity; // 【新增】：环境光强度倍增器

void main() {    
    // 直接采样 HDR 颜色
    vec3 envColor = texture(u_Skybox, v_TexCoords).rgb;
    
    // 乘以强度倍增器，将其校准到匹配当前物理相机的真实亮度级别！
    FragColor = vec4(envColor * u_Intensity, 1.0);
}
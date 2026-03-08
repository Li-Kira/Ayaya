#version 410 core
out vec4 FragColor;

in vec3 v_TexCoords;

// 采样器是 samplerCube 而不是 sampler2D
uniform samplerCube u_Skybox; 

void main() {    
    // 从 Cubemap 中采样环境贴图 (如果有 HDR 可以在这里做个 Tone Mapping)
    vec4 envColor = texture(u_Skybox, v_TexCoords);
    
    // 简单的 Gamma 校正 (假设输入的不是 sRGB 贴图)
    envColor.rgb = pow(envColor.rgb, vec3(1.0/2.2)); 
    
    FragColor = envColor;
}
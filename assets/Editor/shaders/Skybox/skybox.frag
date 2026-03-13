#version 410 core
out vec4 FragColor;

in vec3 v_TexCoords;

// 采样器是 samplerCube 而不是 sampler2D
uniform samplerCube u_Skybox; 

void main() {    
    vec4 envColor = texture(u_Skybox, v_TexCoords);
    
    // 简单的 Gamma 校正 (假设输入的不是 sRGB 贴图)
    envColor.rgb = pow(envColor.rgb, vec3(1.0/2.2)); 
    
    // 赋予天空盒等同于物理天空的亮度 (约 20000 坎德拉/平方米)
    // 这样它才能在 EV100 = 14.5 的极限曝光压缩下，依然保持正常的明亮蔚蓝！
    FragColor = vec4(envColor.rgb * 20000.0, 1.0);
}
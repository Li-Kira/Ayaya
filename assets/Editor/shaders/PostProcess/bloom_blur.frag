#version 410 core
out vec4 FragColor;
in vec2 v_TexCoord;

uniform sampler2D u_Image;
uniform bool u_Horizontal;
// 标准的 9-tap 高斯权重分配
uniform float u_Weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {
    vec2 tex_offset = 1.0 / textureSize(u_Image, 0); // 获取单个像素的尺寸
    vec3 result = texture(u_Image, v_TexCoord).rgb * u_Weight[0]; // 当前像素的贡献
    
    if(u_Horizontal) {
        for(int i = 1; i < 5; ++i) {
            result += texture(u_Image, v_TexCoord + vec2(tex_offset.x * i, 0.0)).rgb * u_Weight[i];
            result += texture(u_Image, v_TexCoord - vec2(tex_offset.x * i, 0.0)).rgb * u_Weight[i];
        }
    } else {
        for(int i = 1; i < 5; ++i) {
            result += texture(u_Image, v_TexCoord + vec2(0.0, tex_offset.y * i)).rgb * u_Weight[i];
            result += texture(u_Image, v_TexCoord - vec2(0.0, tex_offset.y * i)).rgb * u_Weight[i];
        }
    }
    FragColor = vec4(result, 1.0);
}
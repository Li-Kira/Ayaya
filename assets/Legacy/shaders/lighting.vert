#version 330 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;   // 我们的 Mesh 类已经把法线准备好了！
layout(location = 2) in vec2 a_TexCoord;

uniform mat4 u_ViewProjection;
uniform mat4 u_Transform;

out vec2 v_TexCoord;
out vec3 v_Normal;
out vec3 v_FragPos; // 新增：片段在 3D 世界里的绝对坐标

void main() {
    v_TexCoord = a_TexCoord;
    
    // 算出像素在世界里的真实位置
    v_FragPos = vec3(u_Transform * vec4(a_Position, 1.0));
    
    // 魔法：把法线也跟着物体一起旋转！
    // (使用逆转置矩阵防止缩放导致法线变形)
    v_Normal = mat3(transpose(inverse(u_Transform))) * a_Normal; 
    
    gl_Position = u_ViewProjection * vec4(v_FragPos, 1.0);
}
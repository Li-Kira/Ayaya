#version 410 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

// 使用我们之前配置好的相机 UBO
layout(std140) uniform Camera {
    mat4 u_ViewProjection;
    mat4 u_View;            // world→view (must match C++ struct_CameraData layout)
    vec3 u_CameraPosition;
};

uniform mat4 u_Transform;

out vec3 v_FragPos;
out vec3 v_Normal;
out vec2 v_TexCoord;

void main() {
    vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
    v_FragPos = worldPos.xyz;
    
    // 正确处理法线矩阵，防止物体缩放时法线变形
    mat3 normalMatrix = transpose(inverse(mat3(u_Transform)));
    v_Normal = normalMatrix * a_Normal;
    
    v_TexCoord = a_TexCoord;
    gl_Position = u_ViewProjection * worldPos;
}
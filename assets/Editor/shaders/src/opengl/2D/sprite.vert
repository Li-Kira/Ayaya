#version 410 core

layout(std140) uniform Camera {
    mat4 u_ViewProjection;
    mat4 u_View;            // world→view
    vec3 u_CameraPosition;
};

uniform mat4 u_Transform;
out vec2 v_TexCoord;

void main() {
    // 核心魔法：不依赖任何 VBO 数据，凭空生成 1x1 的完美 2D 矩形！
    vec2 pos[4] = vec2[4](
        vec2(-0.5, -0.5),
        vec2( 0.5, -0.5),
        vec2(-0.5,  0.5),
        vec2( 0.5,  0.5)
    );
    vec2 tex[4] = vec2[4](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 1.0)
    );
    
    // gl_VertexID 依次是 0, 1, 2, 3，构成 Triangle Strip (三角形带)
    v_TexCoord = tex[gl_VertexID];
    
    // 将 XY 平面的 Quad 推入 3D 世界
    gl_Position = u_ViewProjection * u_Transform * vec4(pos[gl_VertexID], 0.0, 1.0);
}
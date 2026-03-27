#version 410 core
layout(location = 0) in vec3 a_Position;

uniform mat4 u_LightSpaceMatrix; // 太阳视角的“视图投影矩阵”
uniform mat4 u_Transform;        // 物体的世界变换矩阵

void main() {
    // 将顶点从局部空间直接变换到太阳的光照空间
    gl_Position = u_LightSpaceMatrix * u_Transform * vec4(a_Position, 1.0);
}
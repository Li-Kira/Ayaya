#version 450 core

layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;

layout(set = 0, binding = 0) uniform CameraData {
    mat4 ViewProjection;
    vec3 CameraPosition;
} u_Camera;

layout(push_constant) uniform PushData {
    mat4 ModelMatrix;
    vec4 Albedo;
    vec4 LightDir;
    vec4 LightColor;
    vec4 Ambient;
} u_Push;

void main() {
    vec3 N = normalize(v_Normal);
    vec3 L = normalize(u_Push.LightDir.xyz);
    vec3 V = normalize(u_Camera.CameraPosition - v_WorldPos);
    vec3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, 256.0) * 0.4;

    vec3 diffuse  = u_Push.Albedo.rgb * u_Push.LightColor.rgb * NdotL;
    vec3 ambient  = u_Push.Albedo.rgb * u_Push.Ambient.rgb;
    vec3 specular = u_Push.LightColor.rgb * spec;

    FragColor = vec4(ambient + diffuse + specular, 1.0);
}

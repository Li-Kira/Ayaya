#version 410 core

layout(location = 0) out vec4 FragColor;

in vec3 v_Normal;
in vec3 v_WorldPos;

uniform vec3 u_LightDir;
uniform vec3 u_LightColor;
uniform vec3 u_Ambient;
uniform vec3 u_Albedo;
uniform vec3 u_CameraPos;

void main() {
    vec3 N = normalize(v_Normal);
    vec3 L = normalize(u_LightDir);
    vec3 V = normalize(u_CameraPos - v_WorldPos);
    vec3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, 256.0) * 0.4;

    vec3 diffuse = u_Albedo * u_LightColor * NdotL;
    vec3 ambient = u_Albedo * u_Ambient;
    vec3 specular = u_LightColor * spec;

    FragColor = vec4(ambient + diffuse + specular, 1.0);
}

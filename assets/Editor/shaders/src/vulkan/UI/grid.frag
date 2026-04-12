#version 450 core

layout(location = 0) in vec3 v_WorldPos;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform Constants {
    mat4 u_Transform;
    float u_ExposureInverse;
} pc;

void main() {
    vec2 coord = v_WorldPos.xz;
    vec2 derivative = fwidth(coord);

    vec2 grid1 = abs(fract(coord - 0.5) - 0.5) / derivative;
    float line1 = min(grid1.x, grid1.y);
    
    vec2 grid10 = abs(fract(coord / 10.0 - 0.5) - 0.5) / (derivative / 10.0);
    float line10 = min(grid10.x, grid10.y);

    float alpha1 = 1.0 - min(line1, 1.0);
    float alpha10 = 1.0 - min(line10, 1.0);

    vec3 color = vec3(0.0);
    float finalAlpha = 0.0;

    vec3 colorGrid1 = vec3(0.15);  
    vec3 colorGrid10 = vec3(0.35); 

    if (alpha10 > 0.0) {
        color = colorGrid10;
        finalAlpha = alpha10;
    } else if (alpha1 > 0.0) {
        color = colorGrid1;
        finalAlpha = alpha1 * 0.4; 
    }

    float axisLineWidth = 1.2;

    if (abs(v_WorldPos.z) < derivative.y * axisLineWidth) {
        color = vec3(0.70, 0.15, 0.15);
        finalAlpha = 1.0;
    } else if (abs(v_WorldPos.x) < derivative.x * axisLineWidth) {
        color = vec3(0.15, 0.15, 0.70);
        finalAlpha = 1.0;
    }

    float fade = max(0.0, 1.0 - length(v_WorldPos.xz) / 100.0);
    fade = fade * fade; 

    FragColor = vec4(color * pc.u_ExposureInverse, finalAlpha * fade * 0.7);
    
    if (FragColor.a < 0.01) {
        discard;
    }
}
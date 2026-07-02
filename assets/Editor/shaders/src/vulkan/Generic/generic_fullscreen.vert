#version 450 core

// Full-screen triangle — OpenGL NDC (Y-up) for negative-height viewport.
// Used by GenericFullScreenPass and any post-processing effect.

vec2 positions[3] = vec2[](
    vec2(-1.0,  1.0),
    vec2( 3.0,  1.0),
    vec2(-1.0, -3.0)
);

vec2 uvs[3] = vec2[](
    vec2(0.0, 0.0),
    vec2(2.0, 0.0),
    vec2(0.0, 2.0)
);

layout(location = 0) out vec2 v_TexCoord;

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    v_TexCoord = uvs[gl_VertexIndex];
}

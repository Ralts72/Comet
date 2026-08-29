#version 450

layout(location=0) in vec3 a_Pos;
layout(location=1) in vec2 a_TexCoord;
layout(location=2) in vec3 a_Normal;

out gl_PerVertex{
    vec4 gl_Position;
};

layout(push_constant) uniform PushConstants {
    mat4 matrix;
} pc;

out layout(location=0) vec4 vertexColor;

void main() {
    gl_Position = pc.matrix * vec4(a_Pos.x, -a_Pos.y, a_Pos.z, 1.f);
    vertexColor = vec4(a_Normal.x * 0.5 + 0.5, a_Normal.y * 0.5 + 0.5, a_Normal.z * 0.5 + 0.5, 1.0f);
}
#version 450

layout(location=0) in vec3 a_Pos;
layout(location=1) in vec2 a_TexCoord;
layout(location=2) in vec3 a_Normal;

out gl_PerVertex{
    vec4 gl_Position;
};

layout(set=0, binding=0, std140) uniform ViewProjectMatrix {
    mat4 view;
    mat4 projection;
} matrix;

layout(set=0, binding=1, std140) uniform ModelMatrix {
    mat4 model;
} model;

out layout(location=1) vec2 v_TexCoord;

void main() {
    gl_Position = matrix.projection * matrix.view * model.model * vec4(a_Pos, 1.f);
    v_TexCoord = a_TexCoord;
}
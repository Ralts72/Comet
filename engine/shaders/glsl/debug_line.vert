#version 450

layout(location=0) in vec3 a_Position;
layout(location=1) in vec4 a_Color;

layout(push_constant) uniform DebugDrawConstants {
    mat4 view_projection;
} constants;

layout(location=0) out vec4 v_Color;

void main() {
    gl_Position = constants.view_projection * vec4(a_Position, 1.0);
    v_Color = a_Color;
}

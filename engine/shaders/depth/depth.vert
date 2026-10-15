#version 450
#extension GL_GOOGLE_include_directive : require
#include "../common/instance_transform.glsl"
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
layout(location = 0) out vec2 uv;
void main() {
    uv = texcoord;
    gl_Position = instance_transform() * vec4(position, 1.0);
}

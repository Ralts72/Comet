#version 450
#extension GL_GOOGLE_include_directive : require
#include "../common/instance_transform.glsl"
layout(location = 0) in vec3 position;
void main() {
    gl_Position = instance_transform() * vec4(position, 1.0);
}

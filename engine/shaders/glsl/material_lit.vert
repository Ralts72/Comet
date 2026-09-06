#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
layout(location = 2) in vec3 normal;
layout(location = 0) out vec3 world_position;
layout(location = 1) out vec3 world_normal;
layout(push_constant) uniform ObjectData { mat4 model; } object;

void main() {
    vec4 world = object.model * vec4(position, 1.0);
    world_position = world.xyz;
    mat3 basis = mat3(object.model);
    world_normal = vec3(0.0);
    if(abs(determinant(basis)) > 1e-8)
        world_normal = transpose(inverse(basis)) * normal;
    gl_Position = frame.projection * frame.view * world;
}

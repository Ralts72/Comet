#version 450
#extension GL_GOOGLE_include_directive : require
#include "lighting.glsl"

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec3 world_normal;
layout(location = 0) out vec4 color;
layout(set = 1, binding = 0, std140) uniform MaterialData {
    vec4 albedo;
} material;

void main() {
    color = vec4(clamp(material.albedo.rgb * diffuse_lighting(world_position, world_normal), 0.0, 65504.0),
        material.albedo.a);
}

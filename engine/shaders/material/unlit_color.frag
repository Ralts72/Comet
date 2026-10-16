#version 450
#extension GL_GOOGLE_include_directive : require
#include "../common/surface.glsl"
#include "../common/fog.glsl"

layout(location = 0) out vec4 color;
layout(set = 1, binding = 0, std140) uniform MaterialData {
    vec4 color;
    float intensity;
} material;

void main() {
    color = vec4(apply_distance_fog(material.color.rgb * material.intensity), surface_alpha(material.color.a));
}

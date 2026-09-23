#version 450

layout(location = 0) in vec3 world_position;
layout(location = 0) out vec4 color;

layout(set = 1, binding = 0, std140) uniform MaterialData {
    vec4 color;
    float intensity;
    float frequency;
} material;

void main() {
    float stripe = step(0.0, sin(world_position.x * material.frequency + world_position.y * 6.0));
    vec3 pattern = mix(vec3(0.06, 0.35, 0.85), vec3(0.95, 0.55, 0.12), stripe);
    color = vec4(pattern * material.color.rgb * material.intensity, material.color.a);
}

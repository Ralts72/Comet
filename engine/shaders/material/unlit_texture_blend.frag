#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(set = 1, binding = 0, std140) uniform MaterialData {
    vec4 tint;
    float blend;
} material;
layout(set = 1, binding = 1) uniform sampler2D texture0;
layout(set = 1, binding = 2) uniform sampler2D texture1;

void main() {
    color = material.tint * mix(texture(texture0, uv), texture(texture1, uv),
        clamp(material.blend, 0.0, 1.0));
}

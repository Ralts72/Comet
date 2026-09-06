#version 450

layout(location = 0) out vec4 color;
layout(set = 1, binding = 0, std140) uniform MaterialData {
    vec4 color;
    float intensity;
} material;

void main() {
    color = vec4(material.color.rgb * material.intensity, material.color.a);
}

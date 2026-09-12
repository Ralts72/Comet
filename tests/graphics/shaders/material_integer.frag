#version 450
layout(location = 0) out vec4 color;
layout(set = 1, binding = 0, std140) uniform MaterialData {
    ivec4 color;
    float intensity;
} material;
void main() {
    color = vec4(material.color) * material.intensity;
}

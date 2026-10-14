#version 450

layout(location = 0) in vec2 coordinate;
layout(location = 1) in vec4 tint;
layout(location = 0) out vec4 output_color;

void main() {
    float falloff = 1 - smoothstep(0, 1, dot(coordinate, coordinate));
    output_color = vec4(tint.rgb, tint.a * falloff);
}

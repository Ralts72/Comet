#version 450
layout(push_constant) uniform Outline { vec4 surface; vec4 color; vec4 style; } outline;
layout(location=0) out vec4 color;
layout(location=1) out vec4 style;
void main() { color = outline.color; style = outline.style; }

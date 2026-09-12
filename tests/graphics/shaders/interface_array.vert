#version 450
layout(set = 3, binding = 4) uniform sampler2D images[4];
layout(push_constant) uniform Object {
    layout(offset = 16) vec4 offset;
    layout(offset = 32) float scale;
} object;
void main() {
    gl_Position = object.offset + object.scale * textureLod(images[3], vec2(0.5), 0);
}

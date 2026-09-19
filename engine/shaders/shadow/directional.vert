#version 450
layout(location = 0) in vec3 position;
layout(push_constant) uniform ShadowObject {
    mat4 light_mvp;
} object;
void main() {
    gl_Position = object.light_mvp * vec4(position, 1.0);
}

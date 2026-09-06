#version 450
layout(constant_id = 0) const bool enabled = true;
void main() {
    vec2 positions[3] = vec2[](vec2(-1, -1), vec2(3, -1), vec2(-1, 3));
    gl_Position = vec4(positions[gl_VertexIndex], 0.5, 1);
    if (!enabled) {
        gl_Position = vec4(3, 3, 0.5, 1);
    }
}

#version 450
layout(location = 0) in vec3 position;
layout(push_constant) uniform PushConstant { mat4 model_view_projection; } transform;
void main() {
    gl_Position = transform.model_view_projection * vec4(position, 1.0);
}

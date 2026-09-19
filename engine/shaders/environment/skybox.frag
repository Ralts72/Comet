#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform samplerCube environment_map;
layout(push_constant) uniform Parameters {
    mat4 clip_to_environment;
    float intensity;
} parameters;

void main() {
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 near_point = parameters.clip_to_environment * vec4(ndc, 0.0, 1.0);
    vec4 far_point = parameters.clip_to_environment * vec4(ndc, 1.0, 1.0);
    vec3 direction = far_point.xyz * near_point.w - near_point.xyz * far_point.w;
    color = vec4(texture(environment_map, direction).rgb * parameters.intensity, 1.0);
}

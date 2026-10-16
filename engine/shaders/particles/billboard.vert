#version 450

layout(location = 0) in vec4 position_size;
layout(location = 1) in vec4 color;
layout(location = 2) in vec4 uv_rect;
layout(location = 3) in vec4 end_position_size;
layout(location = 0) out vec2 coordinate;
layout(location = 1) out vec4 tint;
layout(location = 2) flat out vec4 texture_region;
layout(location = 3) flat out uint ribbon;
layout(location = 4) flat out vec2 fog_range;
layout(location = 5) out vec4 clip_position;
layout(push_constant) uniform Camera {
    mat4 view_projection;
    vec4 right;
    vec4 up;
} camera;

const vec2 corners[6] = vec2[](
    vec2(-1, -1), vec2(1, -1), vec2(1, 1),
    vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main() {
    coordinate = corners[gl_VertexIndex];
    tint = color;
    texture_region = uv_rect;
    ribbon = end_position_size.w >= 0.0 ? 1u : 0u;
    vec3 position;
    if(ribbon != 0u) {
        vec3 axis = end_position_size.xyz - position_size.xyz;
        vec3 side = cross(cross(camera.right.xyz, camera.up.xyz), axis);
        side = dot(side, side) > 1e-10 ? normalize(side) : camera.right.xyz;
        float t = coordinate.x * 0.5 + 0.5;
        float width = mix(position_size.w, end_position_size.w, t);
        position = mix(position_size.xyz, end_position_size.xyz, t) + side * coordinate.y * width * 0.5;
    } else {
        vec3 offset = (camera.right.xyz * coordinate.x + camera.up.xyz * coordinate.y)
            * position_size.w * 0.5;
        position = position_size.xyz + offset;
    }
    fog_range = vec2(camera.right.w, camera.up.w);
    clip_position = camera.view_projection * vec4(position, 1);
    gl_Position = clip_position;
}

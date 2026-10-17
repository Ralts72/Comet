#version 450
#extension GL_GOOGLE_include_directive : require
#include "../common/frame.glsl"

layout(push_constant) uniform ProbeDraw {
    vec4 origin_radius;
    vec4 spacing;
    vec4 dimensions_pixel; // grid x/y, inverse target width/height
} parameters;
layout(location = 0) out vec2 coordinate;
layout(location = 1) flat out uint probe;

void main() {
    const vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1),
        vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    probe = gl_InstanceIndex;
    uvec2 dimensions = uvec2(parameters.dimensions_pixel.xy);
    uvec3 cell = uvec3(probe % dimensions.x, (probe / dimensions.x) % dimensions.y,
        probe / (dimensions.x * dimensions.y));
    vec3 position = parameters.origin_radius.xyz + vec3(cell) * parameters.spacing.xyz;
    gl_Position = frame.projection * frame.view * vec4(position, 1.0);
    coordinate = corners[gl_VertexIndex];
    gl_Position.xy += coordinate * parameters.origin_radius.w * 2.0
        * parameters.dimensions_pixel.zw * gl_Position.w;
}

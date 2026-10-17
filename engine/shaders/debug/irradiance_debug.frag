#version 450

#extension GL_GOOGLE_include_directive : require
#include "../lighting/irradiance_texture.glsl"
layout(push_constant) uniform ProbeDraw {
    layout(offset = 48) uvec4 updated; // first, count, total, evaluated prefix
    uvec4 display; // mode, region
} parameters;
layout(location = 0) in vec2 coordinate;
layout(location = 1) flat in uint probe;
layout(location = 0) out vec4 color;

void main() {
    float radius = length(coordinate);
    if(radius > 1.0) discard;
    vec4 first = irradiance_texel(int(parameters.display.y), ivec2(0, probe));
    bool valid = first.w >= 0.5;
    bool evaluated = probe < parameters.updated.w;
    vec3 value = vec3(0.3);
    if(evaluated) value = valid ? vec3(0.1, 1.0, 0.25) : vec3(1.0, 0.1, 0.1);
    if(parameters.display.x == 2u && valid)
        value = max(first.rgb * 0.2820947918, vec3(0.0));
    bool updated = parameters.updated.y != 0u
        && (probe + parameters.updated.z - parameters.updated.x) % parameters.updated.z < parameters.updated.y;
    if(updated && radius > 0.68) value = vec3(1.0, 0.9, 0.05);
    color = vec4(value, 1.0);
}

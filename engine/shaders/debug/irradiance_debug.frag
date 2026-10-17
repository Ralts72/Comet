#version 450

layout(set = 0, binding = 11) uniform sampler2D irradiance_grid;
layout(push_constant) uniform ProbeDraw {
    layout(offset = 48) uvec4 flags; // update first/count, total, mode + evaluated prefix
} parameters;
layout(location = 0) in vec2 coordinate;
layout(location = 1) flat in uint probe;
layout(location = 0) out vec4 color;

void main() {
    float radius = length(coordinate);
    if(radius > 1.0) discard;
    vec4 first = texelFetch(irradiance_grid, ivec2(0, probe), 0);
    bool valid = first.w >= 0.5;
    bool evaluated = probe < (parameters.flags.w >> 2);
    vec3 value = vec3(0.3);
    if(evaluated) value = valid ? vec3(0.1, 1.0, 0.25) : vec3(1.0, 0.1, 0.1);
    if((parameters.flags.w & 3u) == 2u && valid)
        value = max(first.rgb * 0.2820947918, vec3(0.0));
    bool updated = parameters.flags.y != 0u
        && (probe + parameters.flags.z - parameters.flags.x) % parameters.flags.z < parameters.flags.y;
    if(updated && radius > 0.68) value = vec3(1.0, 0.9, 0.05);
    color = vec4(value, 1.0);
}

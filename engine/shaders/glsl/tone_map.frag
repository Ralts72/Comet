#version 450
#extension GL_GOOGLE_include_directive : require
#include "post_process.glsl"

layout(set = 0, binding = 0) uniform sampler2D hdr_color;
layout(set = 0, binding = 1) uniform sampler2D bloom_color;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

vec3 sample_bloom() {
    ivec2 size = textureSize(bloom_color, 0);
    vec2 position = uv * vec2(size) - 0.5;
    ivec2 first = ivec2(floor(position));
    vec2 fraction = fract(position);
    vec3 a = texelFetch(bloom_color, clamp(first, ivec2(0), size - 1), 0).rgb;
    vec3 b = texelFetch(bloom_color, clamp(first + ivec2(1, 0), ivec2(0), size - 1), 0).rgb;
    vec3 c = texelFetch(bloom_color, clamp(first + ivec2(0, 1), ivec2(0), size - 1), 0).rgb;
    vec3 d = texelFetch(bloom_color, clamp(first + ivec2(1, 1), ivec2(0), size - 1), 0).rgb;
    return mix(mix(a, b, fraction.x), mix(c, d, fraction.x), fraction.y);
}

void main() {
    vec3 hdr = max(texture(hdr_color, uv).rgb, vec3(0.0));
    if(parameters.bloom_strength > 0.0)
        hdr += sample_bloom() * parameters.bloom_strength;
    hdr = clamp(hdr, 0.0, 65504.0);
    vec3 mapped = vec3(1.0) - exp(-hdr * parameters.exposure);
    if(parameters.encode_srgb != 0) {
        mapped = mix(1.055 * pow(mapped, vec3(1.0 / 2.4)) - 0.055,
            12.92 * mapped, lessThanEqual(mapped, vec3(0.0031308)));
    }
    color = vec4(mapped, 1.0);
}

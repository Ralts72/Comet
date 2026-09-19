#version 450

layout(set = 0, binding = 0) uniform sampler2D hdr_color;
layout(set = 0, binding = 1) uniform sampler2D bloom_color;
layout(push_constant) uniform Parameters {
    float exposure;
    uint encode_srgb;
    float headroom;
    float bloom_strength;
} parameters;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

// Explicit bilinear upsampling avoids requiring float-format linear filtering support.
vec3 sample_bloom(vec2 coordinate) {
    ivec2 size = textureSize(bloom_color, 0);
    vec2 pixel = coordinate * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(pixel));
    vec2 weight = fract(pixel);
    vec3 a = texelFetch(bloom_color, clamp(base, ivec2(0), size - 1), 0).rgb;
    vec3 b = texelFetch(bloom_color, clamp(base + ivec2(1, 0), ivec2(0), size - 1), 0).rgb;
    vec3 c = texelFetch(bloom_color, clamp(base + ivec2(0, 1), ivec2(0), size - 1), 0).rgb;
    vec3 d = texelFetch(bloom_color, clamp(base + ivec2(1, 1), ivec2(0), size - 1), 0).rgb;
    return mix(mix(a, b, weight.x), mix(c, d, weight.x), weight.y);
}

void main() {
    vec3 hdr = clamp(texture(hdr_color, uv).rgb, 0.0, 65504.0);
    if(parameters.bloom_strength > 0.0)
        hdr = clamp(hdr + sample_bloom(uv) * parameters.bloom_strength, 0.0, 65504.0);
    vec3 mapped = parameters.headroom
        * (vec3(1.0) - exp(-hdr * parameters.exposure / parameters.headroom));
    if(parameters.encode_srgb != 0) {
        mapped = mix(1.055 * pow(mapped, vec3(1.0 / 2.4)) - 0.055,
            12.92 * mapped, lessThanEqual(mapped, vec3(0.0031308)));
    }
    color = vec4(mapped, 1.0);
}

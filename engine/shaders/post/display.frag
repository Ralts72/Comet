#version 450

layout(set = 0, binding = 0) uniform sampler2D hdr_color;
layout(set = 0, binding = 1) uniform sampler2D bloom_color;
layout(set = 0, binding = 2) uniform sampler2D scene_depth;
layout(set = 0, binding = 3) uniform sampler2D motion_vectors;
layout(push_constant) uniform Parameters {
    float exposure;
    uint encode_srgb;
    float headroom;
    float bloom_strength;
    uint upsample_hdr;
    float white_level;
    uint depth_of_field;
    uint motion_blur;
    vec4 projection;
    vec4 focus;
    vec4 motion;

} parameters;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

// Explicit bilinear upsampling avoids requiring float-format linear filtering support.
vec3 sample_bilinear(sampler2D image, vec2 coordinate) {
    ivec2 size = textureSize(image, 0);
    vec2 pixel = coordinate * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(pixel));
    vec2 weight = fract(pixel);
    vec3 a = texelFetch(image, clamp(base, ivec2(0), size - 1), 0).rgb;
    vec3 b = texelFetch(image, clamp(base + ivec2(1, 0), ivec2(0), size - 1), 0).rgb;
    vec3 c = texelFetch(image, clamp(base + ivec2(0, 1), ivec2(0), size - 1), 0).rgb;
    vec3 d = texelFetch(image, clamp(base + ivec2(1, 1), ivec2(0), size - 1), 0).rgb;
    return mix(mix(a, b, weight.x), mix(c, d, weight.x), weight.y);
}

float view_distance(vec2 coordinate) {
    float d = texture(scene_depth, vec2(coordinate.x, 1 - coordinate.y)).r;
    float denominator = d * parameters.projection.z - parameters.projection.x;
    return min(abs((d * parameters.projection.w - parameters.projection.y) / max(abs(denominator), 1e-8)), 100000);
}
vec3 sample_lens() {
    vec2 size = vec2(textureSize(hdr_color, 0));
    vec3 center = sample_bilinear(hdr_color, uv);
    float distance = view_distance(uv);
    float radius = parameters.focus.z * clamp((abs(distance - parameters.focus.x) - parameters.focus.y * .5) / max(distance, .01), 0, 1);
    if(parameters.depth_of_field == 0u) radius = 0;
    vec2 velocity = vec2(0);
    if(parameters.motion_blur != 0u) {
        vec4 motion = texture(motion_vectors, vec2(uv.x, 1 - uv.y));
        if(motion.w > .5) velocity = motion.xy * size * parameters.motion.x;
        velocity *= min(1, parameters.motion.y / max(length(velocity), 1e-4));
    }
    if(radius < .5 && length(velocity) < .5) return center;
    vec3 sum = center;
    float weights = 1;
    // A bounded disk gather also integrates the shutter segment; foreground edges reject distant taps.
    for(int i = 0; i < 16; ++i) {
        float t = (float(i) + .5) / 16;
        float angle = float(i) * 2.39996323;
        vec2 offset = vec2(cos(angle), sin(angle)) * sqrt(t) * radius + velocity * (t - .5);
        vec2 q = clamp(uv + offset / size, vec2(0), vec2(1));
        float other = view_distance(q);
        float weight = other > distance ? exp(-abs(other - distance) / max(distance * .05, .05)) : 1;
        sum += sample_bilinear(hdr_color, q) * weight;
        weights += weight;
    }
    return sum / weights;
}
void main() {
    vec3 hdr;
    if(parameters.depth_of_field != 0u || parameters.motion_blur != 0u)
        hdr = sample_lens();
    else if(parameters.upsample_hdr != 0)
        hdr = sample_bilinear(hdr_color, uv);
    else
        hdr = texture(hdr_color, uv).rgb;
    hdr = clamp(hdr, 0.0, 65504.0);
    if(parameters.bloom_strength > 0.0)
        hdr = clamp(hdr + sample_bilinear(bloom_color, uv) * parameters.bloom_strength, 0.0, 65504.0);
    vec3 mapped = parameters.white_level * parameters.headroom
        * (vec3(1.0) - exp(-hdr * parameters.exposure / parameters.headroom));
    if(parameters.encode_srgb != 0) {
        mapped = mix(1.055 * pow(mapped, vec3(1.0 / 2.4)) - 0.055,
            12.92 * mapped, lessThanEqual(mapped, vec3(0.0031308)));
    }
    color = vec4(mapped, 1.0);
}

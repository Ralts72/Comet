#version 450
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(set = 0, binding = 0) uniform sampler2D current_color;
layout(set = 0, binding = 1) uniform sampler2D current_depth;
layout(set = 0, binding = 2) uniform sampler2D motion_vectors;
layout(set = 0, binding = 3) uniform sampler2D previous_color;
layout(push_constant) uniform Parameters { vec4 projection; vec2 jitter_delta; uint valid; } parameters;

vec3 to_ycocg(vec3 c) { return vec3(dot(c, vec3(.25, .5, .25)), c.r - c.b, c.g - (c.r + c.b) * .5); }
vec3 from_ycocg(vec3 c) { return vec3(c.x - c.z * .5 + c.y * .5, c.x + c.z * .5, c.x - c.z * .5 - c.y * .5); }
vec4 sample_history(vec2 coordinate) {
    ivec2 size = textureSize(previous_color, 0);
    vec2 pixel = coordinate * vec2(size) - .5;
    ivec2 base = ivec2(floor(pixel));
    vec2 fraction = fract(pixel);
    vec4 a = texelFetch(previous_color, clamp(base, ivec2(0), size - 1), 0);
    vec4 b = texelFetch(previous_color, clamp(base + ivec2(1, 0), ivec2(0), size - 1), 0);
    vec4 c = texelFetch(previous_color, clamp(base + ivec2(0, 1), ivec2(0), size - 1), 0);
    vec4 d = texelFetch(previous_color, clamp(base + ivec2(1, 1), ivec2(0), size - 1), 0);
    return mix(mix(a, b, fraction.x), mix(c, d, fraction.x), fraction.y);
}
void main() {
    ivec2 size = textureSize(current_color, 0);
    ivec2 pixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - 1);
    vec4 current_sample = texelFetch(current_color, pixel, 0);
    vec3 current = current_sample.rgb;
    vec2 depth_uv = vec2(uv.x, 1 - uv.y);
    float depth = texture(current_depth, depth_uv).r;
    // Use the nearest visible surface around silhouettes for reprojection.
    vec4 motion = texture(motion_vectors, depth_uv);
    float nearest = depth;
    vec2 texel = 1.0 / vec2(size);
    for(int y = -1; y <= 1; ++y) for(int x = -1; x <= 1; ++x) {
        vec2 q = depth_uv + vec2(x, y) * texel;
        float d = texture(current_depth, q).r;
        if(d < nearest) { nearest = d; motion = texture(motion_vectors, q); }
    }
    float distance = abs((nearest * parameters.projection.w - parameters.projection.y)
        / (nearest * parameters.projection.z - parameters.projection.x));
    result = vec4(current, min(distance, 65504));
    if(parameters.valid == 0u || nearest >= 1) return;
    vec2 previous_uv = uv - motion.xy + parameters.jitter_delta;
    if(motion.w < .5 || any(lessThan(previous_uv, vec2(0))) || any(greaterThan(previous_uv, vec2(1)))) return;
    vec4 previous = sample_history(previous_uv);
    if(abs(previous.a - motion.z) > max(.03, motion.z * .02)) return;
    vec3 lower = vec3(1e20), upper = vec3(-1e20), mean = vec3(0), square = vec3(0);
    for(int y = -1; y <= 1; ++y) for(int x = -1; x <= 1; ++x) {
        vec3 c = to_ycocg(texelFetch(current_color, clamp(pixel + ivec2(x, y), ivec2(0), size - 1), 0).rgb);
        lower = min(lower, c); upper = max(upper, c); mean += c; square += c * c;
    }
    mean /= 9; square /= 9;
    vec3 deviation = sqrt(max(square - mean * mean, vec3(0)));
    lower = max(lower, mean - 1.25 * deviation);
    upper = min(upper, mean + 1.25 * deviation);
    vec3 old = from_ycocg(clamp(to_ycocg(previous.rgb), lower, upper));
    // HDR alpha records transparent coverage; particles and glass reject opaque history.
    float reactive = clamp(current_sample.a * 4, 0, 1);
    float speed = length((motion.xy - parameters.jitter_delta) * vec2(size));
    float weight = .9 * (1 - reactive) / (1 + speed * .05);
    result.rgb = mix(current, old, weight);
}

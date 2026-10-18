#version 450
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 filtered;
layout(location = 1) out vec4 saved_guide;
layout(set = 0, binding = 0) uniform sampler2D noisy_color;
layout(set = 0, binding = 1) uniform sampler2D current_guide;
layout(set = 0, binding = 2) uniform sampler2D reprojection;
layout(set = 0, binding = 3) uniform sampler2D previous_color;
layout(set = 0, binding = 4) uniform sampler2D previous_guide;
layout(push_constant) uniform Parameters {
    vec4 projection;
    vec4 previous_projection;
    float weight;
    uint valid;
} parameters;
vec3 decode_normal(vec2 value) {
    vec3 normal = vec3(value, 1 - abs(value.x) - abs(value.y));
    if(normal.z < 0) normal.xy = (1 - abs(normal.yx)) * mix(vec2(-1), vec2(1), greaterThanEqual(normal.xy, vec2(0)));
    return normalize(normal);
}
float distance_at(float depth, vec4 projection) {
    return min(abs((depth * projection.w - projection.y) / max(abs(depth * projection.z - projection.x), 1e-8)), 100000);
}
void main() {
    ivec2 size = textureSize(noisy_color, 0);
    ivec2 center = clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - 1);
    vec4 guide = texelFetch(current_guide, center, 0);
    vec3 normal = decode_normal(guide.xy);
    float depth = distance_at(guide.z, parameters.projection);
    vec3 mean = vec3(0), square = vec3(0);
    float total = 0;
    for(int y = -2; y <= 2; ++y) for(int x = -2; x <= 2; ++x) {
        ivec2 p = clamp(center + ivec2(x, y), ivec2(0), size - 1);
        vec4 other = texelFetch(current_guide, p, 0);
        float delta = abs(depth - distance_at(other.z, parameters.projection));
        float weight = exp(-.25 * float(x*x + y*y) - delta / max(.02, depth * .015));
        weight *= pow(max(dot(normal, decode_normal(other.xy)), 0), 32);
        if(other.w != guide.w) weight = 0;
        vec3 value = max(texelFetch(noisy_color, p, 0).rgb, vec3(0));
        mean += value * weight;
        square += value * value * weight;
        total += weight;
    }
    mean /= max(total, 1e-6);
    square /= max(total, 1e-6);
    filtered = vec4(mean, 1);
    saved_guide = guide;
    vec4 motion = texelFetch(reprojection, center, 0);
    if(parameters.valid == 0u || motion.w < .5 || any(lessThan(motion.xy, vec2(0))) || any(greaterThanEqual(motion.xy, vec2(1)))) return;
    vec4 old_guide = texture(previous_guide, motion.xy);
    float previous_depth = distance_at(motion.z, parameters.previous_projection);
    float old_depth = distance_at(old_guide.z, parameters.previous_projection);
    if(guide.w != old_guide.w || abs(previous_depth - old_depth) > max(.03, previous_depth * .02) || dot(normal, decode_normal(old_guide.xy)) < .85) return;
    vec4 old = texture(previous_color, motion.xy);
    vec3 deviation = sqrt(max(square - mean * mean, vec3(0))) * 1.5 + vec3(.01);
    vec3 history = clamp(old.rgb, max(vec3(0), mean - deviation), mean + deviation);
    float samples = min(old.a + 1, 32);
    float weight = min(parameters.weight, 1 - 1 / samples);
    filtered = vec4(mix(mean, history, weight), samples);
}

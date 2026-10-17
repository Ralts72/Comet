#version 450
#extension GL_GOOGLE_include_directive : require
#include "../lighting/surface_lighting.glsl"
#include "../common/fog.glsl"
#include "../lighting/volumetric_fog.glsl"
layout(set = 1, binding = 0) uniform texture2D base_metallic_image;
layout(set = 1, binding = 1) uniform texture2D normal_roughness_image;
layout(set = 1, binding = 2) uniform texture2D emission_occlusion_image;
layout(set = 1, binding = 3) uniform texture2D geometry_image;
layout(set = 1, binding = 4) uniform texture2D gbuffer_depth_image;
layout(set = 1, binding = 5) uniform texture2D scene_color_image;
layout(set = 1, binding = 6) uniform texture2D scene_depth_image;
layout(set = 1, binding = 7) uniform sampler point_sampler;
layout(set = 1, binding = 8) uniform sampler linear_sampler;
#define base_metallic sampler2D(base_metallic_image, point_sampler)
#define normal_roughness sampler2D(normal_roughness_image, point_sampler)
#define emission_occlusion sampler2D(emission_occlusion_image, point_sampler)
#define geometry sampler2D(geometry_image, point_sampler)
#define gbuffer_depth sampler2D(gbuffer_depth_image, point_sampler)
#define scene_color sampler2D(scene_color_image, linear_sampler)
#define scene_depth sampler2D(scene_depth_image, point_sampler)
layout(push_constant) uniform Parameters { vec4 values; } parameters;
layout(location = 0) out vec4 color;

vec3 world_position(vec2 uv, float depth) {
    vec4 world = lighting.inverse_view_projection * vec4(uv * vec2(2, -2) + vec2(-1, 1), depth, 1);
    return world.xyz / world.w;
}

bool project_ray(vec4 start, vec4 direction, float distance, out vec2 uv, out float depth) {
    vec4 clip = start + direction * distance;
    if(clip.w <= 0) return false;
    vec3 ndc = clip.xyz / clip.w;
    uv = ndc.xy * vec2(.5, -.5) + .5;
    depth = ndc.z;
    return all(greaterThan(uv, vec2(0))) && all(lessThan(uv, vec2(1))) && depth > 0 && depth < 1;
}

float depth_at(vec2 uv) {
    ivec2 size = textureSize(scene_depth, 0);
    return texelFetch(scene_depth, clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1), 0).r;
}

// 只接受可见 G-buffer 表面；Forward 遮挡者、透明层和天空不能冒充命中。
bool surface_at(vec2 uv, out float depth) {
    ivec2 size = textureSize(scene_depth, 0);
    ivec2 pixel = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
    depth = texelFetch(scene_depth, pixel, 0).r;
    return depth < 1 && abs(depth - texelFetch(gbuffer_depth, pixel, 0).r) < 1e-6
        && texelFetch(geometry, pixel, 0).w > .5 && texelFetch(scene_color, pixel, 0).a < .01;
}

bool trace(vec3 origin, vec3 direction, out vec2 hit_uv, out float confidence) {
    // 线性变换只做一次；步进在齐次裁剪空间中插值，透视除法仍逐点进行。
    vec4 view_start = frame.view * vec4(origin, 1);
    vec4 view_direction = frame.view * vec4(direction, 0);
    vec4 clip_start = frame.projection * view_start;
    vec4 clip_direction = frame.projection * view_direction;
    float previous = 0;
    bool in_front = false;
    float distance_limit = parameters.values.y;
    float thickness = parameters.values.z;
    for(int step = 1; step <= 64; ++step) {
        float distance = distance_limit * float(step * step) / (64.0 * 64.0);
        vec2 uv; float projected_depth;
        if(!project_ray(clip_start, clip_direction, distance, uv, projected_depth)) break;
        // 粗步进和二分只读深度，确认交点后才验证 G-buffer 身份和 Forward／透明遮挡。
        float surface_depth = depth_at(uv);
        float delta = abs(view_start.z + view_direction.z * distance) - view_distance_at_depth(surface_depth);
        if(surface_depth >= 1 || delta < 0) { previous = distance; in_front = true; continue; }
        if(!in_front) continue;
        float low = previous, high = distance;
        for(int refine = 0; refine < 6; ++refine) {
            float middle = (low + high) * .5;
            vec2 sample_uv; float sample_depth;
            if(!project_ray(clip_start, clip_direction, middle, sample_uv, sample_depth)) break;
            float depth = depth_at(sample_uv);
            float difference = abs(view_start.z + view_direction.z * middle) - view_distance_at_depth(depth);
            if(depth >= 1 || difference < 0) low = middle;
            else high = middle;
        }
        if(!project_ray(clip_start, clip_direction, high, hit_uv, projected_depth)
            || !surface_at(hit_uv, surface_depth)) {
            previous = distance;
            continue;
        }
        delta = abs(view_start.z + view_direction.z * high) - view_distance_at_depth(surface_depth);
        if(delta < 0 || delta > thickness) continue;
        ivec2 hit_pixel = ivec2(hit_uv * vec2(textureSize(geometry, 0)));
        vec3 hit_normal = texelFetch(geometry, hit_pixel, 0).xyz;
        if(dot(hit_normal, hit_normal) > .5 && dot(hit_normal, -direction) <= 0) continue;
        vec2 edge = min(hit_uv, 1.0 - hit_uv);
        confidence = smoothstep(0, .08, min(edge.x, edge.y))
            * (1.0 - smoothstep(thickness * .25, thickness, delta))
            * (1.0 - smoothstep(distance_limit * .8, distance_limit, high));
        return confidence > 0;
    }
    return false;
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(scene_color, 0));
    color = texelFetch(scene_color, pixel, 0);
    float depth;
    vec4 geometric = texelFetch(geometry, pixel, 0);
    vec4 surface = texelFetch(normal_roughness, pixel, 0);
    if(geometric.w < .5 || geometric.w > 1.5 || !surface_at(uv, depth)
        || surface.w >= parameters.values.w || dot(surface.xyz, surface.xyz) < .5) return;
    vec3 position = world_position(uv, depth);
    vec3 normal = normalize(surface.xyz);
    vec3 view = frame.orthographic > .5 ? frame.view_direction : normalize(frame.camera_position - position);
    if(dot(normal, view) <= 0) return;
    vec3 reflected = normalize(reflect(-view, normal));
    if(dot(reflected, geometric.xyz) <= 0) return;
    vec2 hit_uv; float confidence;
    if(!trace(position + geometric.xyz * max(.005, parameters.values.z * .05), reflected, hit_uv, confidence)) return;
    float roughness = surface.w;
    float weight = parameters.values.x * confidence * (1.0 - smoothstep(.045, parameters.values.w, roughness));
    vec4 base = texelFetch(base_metallic, pixel, 0);
    float occlusion = texelFetch(emission_occlusion, pixel, 0).a
        * ambient_visibility(position, geometric.xyz);
    vec3 energy = environment_reflected_energy(clamp(dot(normal, view), 0, 1), base.rgb, base.a, roughness);
    vec2 blur = vec2(roughness * roughness * 6) / vec2(textureSize(scene_color, 0));
    vec3 radiance = textureLod(scene_color, hit_uv, 0).rgb * .5;
    radiance += (textureLod(scene_color, hit_uv + vec2(blur.x, 0), 0).rgb
        + textureLod(scene_color, hit_uv - vec2(blur.x, 0), 0).rgb
        + textureLod(scene_color, hit_uv + vec2(0, blur.y), 0).rgb
        + textureLod(scene_color, hit_uv - vec2(0, blur.y), 0).rgb) * .125;
    vec3 fallback = environment_specular_radiance(position, normal, view, roughness);
    float transmittance = distance_fog_transmittance(depth) * volumetric_fog_transmittance(position);
    color.a = max(color.a, weight);
    color.rgb = clamp(color.rgb + weight * (radiance - fallback) * energy * occlusion * transmittance, 0, 65504);
}

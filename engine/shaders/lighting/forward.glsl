#ifndef COMET_FORWARD_LIGHTING_GLSL
#define COMET_FORWARD_LIGHTING_GLSL
#ifndef COMET_SHADOW_PCF_RADIUS
#define COMET_SHADOW_PCF_RADIUS 1
#endif
#include "irradiance_texture.glsl"
const int LIGHT_DIRECTIONAL = 0;
const int LIGHT_POINT = 1;
const int LIGHT_SPOT = 2;

struct Light {
    vec3 position;
    float type;
    vec3 direction;
    float range;
    vec3 color;
    float intensity;
    float inner_cone_cos;
    float outer_cone_cos;
    float casts_shadow;
    float shadow_distance;
};
struct Shadow {
    mat4 view_projection;
    vec4 atlas_rect;
    float depth_bias;
    float texel_size;
    float light_index;
    float reserved;
};
struct ReflectionProbe {
    vec4 position_intensity;
    vec4 minimum_blend;
    vec4 maximum_lod;
    vec4 rotation_projection;
};
#include "irradiance_grid.glsl"
layout(set = 0, binding = 1, std140) uniform LightingData {
    Light lights[32];
    float light_count;
    float excess_lights;
    float invalid_lights;
    float reserved;
    Shadow shadows[20];
    vec4 shadow_splits;
    vec4 shadow_camera_depth;
    float shadow_cascade_count;
    float spot_shadow_count;
    float excess_spot_shadows;
    float shadow_blend;
    float point_shadow_count;
    float excess_point_shadows;
    vec2 shadow_reserved;
    mat4 inverse_view_projection;
    vec4 ao_parameters;
    vec4 environment; // intensity, max LOD, sin(rotation), cos(rotation)
    ReflectionProbe reflection_probes[4];
    vec4 reflection_metadata; // active count, excess count
    IrradianceVolume irradiance_volumes[MAX_IRRADIANCE_VOLUMES];
    vec4 irradiance_metadata;
    vec4 fog_parameters;
} lighting;
layout(set = 0, binding = 2) uniform sampler2D shadow_map;

float sample_shadow_view(int shadow_view, vec3 position, vec3 position_dx, vec3 position_dy, float n_dot_l) {
    Shadow shadow = lighting.shadows[shadow_view];
    mat4 matrix = shadow.view_projection;
    vec4 clip = matrix * vec4(position, 1.0);
    if(clip.w <= 0.0)
        return 1.0;
    vec3 projected = clip.xyz / clip.w;
    vec2 uv = projected.xy * 0.5 + 0.5;
    vec4 clip_dx = matrix * vec4(position_dx, 0.0);
    vec4 clip_dy = matrix * vec4(position_dy, 0.0);
    vec3 projected_dx = (clip_dx.xyz - projected * clip_dx.w) / clip.w;
    vec3 projected_dy = (clip_dy.xyz - projected * clip_dy.w) / clip.w;
    vec2 uv_dx = projected_dx.xy * 0.5;
    vec2 uv_dy = projected_dy.xy * 0.5;
    float depth_dx = projected_dx.z;
    float depth_dy = projected_dy.z;
    float determinant = uv_dx.x * uv_dy.y - uv_dx.y * uv_dy.x;
    vec2 depth_gradient = vec2(0.0);
    if(abs(determinant) > 1e-12)
        depth_gradient = vec2(uv_dy.y * depth_dx - uv_dx.y * depth_dy,
            uv_dx.x * depth_dy - uv_dy.x * depth_dx) / determinant;
    if(projected.z < 0.0 || projected.z > 1.0
        || any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        return 1.0;
    float bias = (shadow.depth_bias / clip.w) * (1.0 + 4.0 * (1.0 - n_dot_l));
    // Compensate nearest-texel quantization on the receiver plane.
    bias += 0.5 * shadow.texel_size * dot(abs(depth_gradient), vec2(1.0));
    float visible = 0.0;
    for(int y = -COMET_SHADOW_PCF_RADIUS; y <= COMET_SHADOW_PCF_RADIUS; ++y) {
        for(int x = -COMET_SHADOW_PCF_RADIUS; x <= COMET_SHADOW_PCF_RADIUS; ++x) {
            vec2 offset = vec2(x, y) * shadow.texel_size;
            vec2 sample_uv = uv + offset;
            if(any(lessThan(sample_uv, vec2(0.0))) || any(greaterThan(sample_uv, vec2(1.0))))
                visible += 1.0;
            else {
                float receiver_depth = projected.z + dot(depth_gradient, offset) - bias;
                // Clamp inside this tile: PCF must not read another light or cascade.
                float inset = 0.5 * shadow.texel_size;
                sample_uv = clamp(sample_uv, vec2(inset), vec2(1.0 - inset));
                vec2 atlas_uv = sample_uv * shadow.atlas_rect.xy + shadow.atlas_rect.zw;
                float sampled_depth = textureLod(shadow_map, atlas_uv, 0.0).r;
                visible += receiver_depth <= sampled_depth ? 1.0 : 0.0;
            }
        }
    }
    return visible / float((2 * COMET_SHADOW_PCF_RADIUS + 1) * (2 * COMET_SHADOW_PCF_RADIUS + 1));
}

// Same +X, -X, +Y, -Y, +Z, -Z order as LightingData; guard texels cover PCF across seams.
int point_shadow_face(vec3 direction) {
    vec3 magnitude = abs(direction);
    if(magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
        return direction.x >= 0.0 ? 0 : 1;
    if(magnitude.y >= magnitude.z)
        return direction.y >= 0.0 ? 2 : 3;
    return direction.z >= 0.0 ? 4 : 5;
}

// Position derivatives must be computed before divergent light/range branches.
float shadow_visibility(int index, vec3 position, vec3 position_dx, vec3 position_dy, float n_dot_l) {
    if(lighting.lights[index].type == LIGHT_POINT) {
        for(int slot = 0; slot < int(lighting.point_shadow_count); ++slot) {
            int first = 8 + slot * 6;
            if(index == int(lighting.shadows[first].light_index)) {
                int face = point_shadow_face(position - lighting.lights[index].position);
                return sample_shadow_view(first + face, position, position_dx, position_dy, n_dot_l);
            }
        }
        return 1.0;
    }
    if(lighting.lights[index].type == LIGHT_SPOT) {
        for(int slot = 0; slot < int(lighting.spot_shadow_count); ++slot) {
            int shadow_view = 4 + slot;
            if(index == int(lighting.shadows[shadow_view].light_index))
                return sample_shadow_view(shadow_view, position, position_dx, position_dy, n_dot_l);
        }
        return 1.0;
    }
    if(lighting.shadow_cascade_count < 1.0 || index != int(lighting.shadows[0].light_index))
        return 1.0;
    float depth = dot(lighting.shadow_camera_depth, vec4(position, 1.0));
    int count = int(lighting.shadow_cascade_count);
    if(depth > lighting.shadow_splits[count - 1])
        return 1.0;
    int cascade = 0;
    while(cascade + 1 < count && depth > lighting.shadow_splits[cascade])
        ++cascade;
    float visible = sample_shadow_view(cascade, position, position_dx, position_dy, n_dot_l);
    float start = cascade == 0 ? 0.0 : lighting.shadow_splits[cascade - 1];
    float end = lighting.shadow_splits[cascade];
    float blend_start = end - (end - start) * lighting.shadow_blend;
    if(depth > blend_start) {
        float next = 1.0;
        if(cascade + 1 < count)
            next = sample_shadow_view(cascade + 1, position, position_dx, position_dy, n_dot_l);
        visible = mix(visible, next, smoothstep(blend_start, end, depth));
    }
    return visible;
}

bool sample_light(int index, vec3 position, out vec3 direction, out vec3 radiance) {
    Light light = lighting.lights[index];
    int type = int(light.type);
    direction = -light.direction;
    float attenuation = 1.0;
    if(type != LIGHT_DIRECTIONAL) {
        vec3 delta = light.position - position;
        float distance_squared = dot(delta, delta);
        if(distance_squared < 1e-8 || distance_squared >= light.range * light.range
            || isnan(distance_squared))
            return false;
        float distance = sqrt(distance_squared);
        direction = delta / distance;
        float falloff = max(1.0 - pow(distance / light.range, 4.0), 0.0);
        attenuation = falloff * falloff / max(distance_squared, 0.01);
        if(type == LIGHT_SPOT) {
            float alignment = dot(-direction, light.direction);
            float cone_weight = step(light.outer_cone_cos, alignment);
            // 极窄锥角在 float 中可能拥有相同 cos 值，退化为硬边而不是除以零。
            if(light.inner_cone_cos - light.outer_cone_cos > 1e-6)
                cone_weight = smoothstep(light.outer_cone_cos, light.inner_cone_cos, alignment);
            attenuation *= cone_weight;
        }
    }
    radiance = light.color * light.intensity * attenuation;
    return true;
}
#endif

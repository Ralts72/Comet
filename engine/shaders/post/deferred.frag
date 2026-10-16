#version 450
#extension GL_GOOGLE_include_directive : require
#include "../lighting/surface_lighting.glsl"
#include "../common/fog.glsl"
#include "../lighting/volumetric_fog.glsl"

layout(set = 1, binding = 0) uniform sampler2D base_metallic;
layout(set = 1, binding = 1) uniform sampler2D normal_roughness;
layout(set = 1, binding = 2) uniform sampler2D emission_occlusion;
layout(set = 1, binding = 3) uniform sampler2D geometry;
layout(set = 1, binding = 4) uniform sampler2D scene_depth;
layout(location = 0) out vec4 color;

vec3 world_position(vec2 coordinate, float depth) {
    vec2 clip = vec2(coordinate.x, 1.0 - coordinate.y) * 2.0 - 1.0;
    vec4 world = lighting.inverse_view_projection * vec4(clip, depth, 1.0);
    return world.xyz / world.w;
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec2 size = vec2(textureSize(scene_depth, 0));
    vec2 coordinate = gl_FragCoord.xy / size;
    float depth = texelFetch(scene_depth, pixel, 0).r;
    vec4 geometric = texelFetch(geometry, pixel, 0);
    vec3 position = world_position(coordinate, depth);
    // 导数在丢弃背景之前求值，避免 quad 内非一致控制流。
    vec3 position_dx = dFdx(position), position_dy = dFdy(position);
    if(geometric.w < 0.5 || depth >= 1.0)
        discard;
    vec4 base = texelFetch(base_metallic, pixel, 0);
    vec4 surface = texelFetch(normal_roughness, pixel, 0);
    vec4 emission = texelFetch(emission_occlusion, pixel, 0);
    vec3 result = emission.rgb;
    vec3 view = frame.orthographic > 0.5 ? frame.view_direction : frame.camera_position - position;
    if(geometric.w < 1.5 && length(surface.xyz) > 1e-6 && length(view) > 1e-6)
        result += evaluate_surface_lighting(position, normalize(surface.xyz), geometric.xyz,
            normalize(view), base.rgb, base.a, surface.w, emission.a, position_dx, position_dy);
    result = apply_volumetric_fog(apply_distance_fog_at_depth(result, depth), position);
    color = vec4(clamp(result, 0.0, 65504.0), 0.0);
    // 将实际不透明深度交给后续 Forward 绘制；采样的是独立 G-buffer 深度附件。
    gl_FragDepth = depth;
}

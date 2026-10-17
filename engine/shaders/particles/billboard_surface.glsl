#include "../common/volumetric_fog.glsl"

layout(location = 0) in vec2 coordinate;
layout(location = 1) in vec4 tint;
layout(location = 2) flat in vec4 texture_region;
layout(location = 3) flat in uint ribbon;
layout(location = 4) flat in vec2 fog_range;
layout(location = 5) in vec4 clip_position;
layout(location = 0) out vec4 output_color;
#ifdef COMET_OIT
#include "../common/transparency.glsl"
#endif
layout(set = 0, binding = 0) uniform sampler2D particle_texture;
layout(set = 1, binding = 0) uniform sampler2D scene_depth;
layout(set = 1, binding = 1) uniform sampler2D fog_volume;
layout(push_constant) uniform TextureSettings {
    layout(offset = 96) vec4 depth_coefficients;
    vec4 surface; // textured, soft distance, additive
} settings;

void main() {
    float radius = ribbon != 0u ? coordinate.y * coordinate.y : dot(coordinate, coordinate);
    float falloff = 1 - smoothstep(0, 1, radius);
    if(settings.surface.x != 0.0) {
        vec2 local_uv = vec2(coordinate.x, -coordinate.y) * 0.5 + 0.5;
        vec2 uv = texture_region.xy + local_uv * texture_region.zw;
        if(any(lessThan(texture_region.zw, vec2(1)))) {
            // 图集未带逐格 mip padding；固定基础 mip，并收缩到纹素中心避免相邻帧串色。
            vec2 inset = min(0.5 / vec2(textureSize(particle_texture, 0)), texture_region.zw * 0.5);
            uv = clamp(uv, texture_region.xy + inset, texture_region.xy + texture_region.zw - inset);
            output_color = tint * textureLod(particle_texture, uv, 0.0);
        } else
            output_color = tint * texture(particle_texture, uv);
    } else
        output_color = vec4(tint.rgb, tint.a * falloff);
    if(settings.surface.y > 0.0) {
        ivec2 pixel = ivec2(gl_FragCoord.xy);
        // 相机深度沿用阴影通道的正高度 viewport，主场景使用负高度。
        pixel.y = textureSize(scene_depth, 0).y - 1 - pixel.y;
        float depth = texelFetch(scene_depth, pixel, 0).r;
        if(depth < 1.0) {
        vec4 coefficients = settings.depth_coefficients;
        float scene_z = (coefficients.x * depth + coefficients.y) / (coefficients.z * depth + coefficients.w);
        float particle_z = (coefficients.x * gl_FragCoord.z + coefficients.y)
            / (coefficients.z * gl_FragCoord.z + coefficients.w);
        float fade = clamp((abs(scene_z) - abs(particle_z)) / settings.surface.y, 0.0, 1.0);
        output_color.a *= fade;
        }
    }
    if(fog_range.x > 0) {
        vec4 coefficients = settings.depth_coefficients;
        float z = (coefficients.x * gl_FragCoord.z + coefficients.y)
            / (coefficients.z * gl_FragCoord.z + coefficients.w);
        vec2 uv = clip_position.xy / clip_position.w * vec2(.5, -.5) + .5;
        vec4 fog = sample_volumetric_fog(fog_volume, fog_range, uv, abs(z));
        output_color.rgb *= fog.a;
        if(settings.surface.z == 0) output_color.rgb += fog.rgb;
    }
#ifdef COMET_OIT
    accumulate_transparency(output_color, settings.surface.z != 0);
#endif
}

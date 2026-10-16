#version 450

layout(location = 0) in vec2 coordinate;
layout(location = 1) in vec4 tint;
layout(location = 2) flat in vec4 texture_region;
layout(location = 0) out vec4 output_color;
layout(set = 0, binding = 0) uniform sampler2D particle_texture;
layout(set = 1, binding = 0) uniform sampler2D scene_depth;
layout(push_constant) uniform TextureSettings {
    layout(offset = 96) vec4 depth_coefficients;
    vec2 surface; // textured, soft distance
} settings;

void main() {
    float falloff = 1 - smoothstep(0, 1, dot(coordinate, coordinate));
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
        if(depth >= 1.0) return;
        vec4 coefficients = settings.depth_coefficients;
        float scene_z = (coefficients.x * depth + coefficients.y) / (coefficients.z * depth + coefficients.w);
        float particle_z = (coefficients.x * gl_FragCoord.z + coefficients.y)
            / (coefficients.z * gl_FragCoord.z + coefficients.w);
        float fade = clamp((abs(scene_z) - abs(particle_z)) / settings.surface.y, 0.0, 1.0);
        output_color.a *= fade;
    }
}

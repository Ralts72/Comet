#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec4 colour;
layout(location = 2) in vec2 tex_coord;

layout(location = 0) out vec4 linear_colour;
layout(location = 1) out vec2 uv;

layout(push_constant) uniform Geometry {
    mat4 transform;
    vec2 translation;
    vec2 dimensions;
} geometry;

vec3 srgb_to_linear(vec3 value) {
    return mix(value / 12.92, pow((value + 0.055) / 1.055, vec3(2.4)),
        greaterThan(value, vec3(0.04045)));
}

void main() {
    vec4 pixel = geometry.transform * vec4(position + geometry.translation, 0.0, 1.0);
    gl_Position = vec4(2.0 * pixel.xy / geometry.dimensions - pixel.ww, 0.0, pixel.w);
    // RmlUi 提供 sRGB 预乘颜色，在线性域插值之前解码并重新预乘。
    linear_colour = vec4(0.0);
    if(colour.a > 0.0)
        linear_colour = vec4(srgb_to_linear(clamp(colour.rgb / colour.a, 0.0, 1.0)) * colour.a,
            colour.a);
    uv = tex_coord;
}

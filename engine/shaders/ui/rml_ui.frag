#version 450

layout(location = 0) in vec4 linear_colour;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec4 output_colour;

layout(set = 0, binding = 0) uniform sampler2D image;
layout(push_constant) uniform Output {
    layout(offset = 80) uint encode_srgb;
} output_options;

vec3 linear_to_srgb(vec3 value) {
    return mix(value * 12.92, 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055,
        greaterThan(value, vec3(0.0031308)));
}

void main() {
    // Atlas 在上传时已经转换到线性预乘域，双线性采样不会产生透明边缘色晕。
    output_colour = linear_colour * texture(image, uv);
    if(output_options.encode_srgb != 0 && output_colour.a > 0.0)
        output_colour.rgb = linear_to_srgb(clamp(output_colour.rgb / output_colour.a, 0.0, 1.0))
                            * output_colour.a;
}

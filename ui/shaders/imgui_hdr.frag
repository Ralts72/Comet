#version 450 core

layout(location = 0) out vec4 fColor;
layout(set = 0, binding = 0) uniform sampler2D sTexture;
layout(location = 0) in struct { vec4 Color; vec2 UV; } In;

vec3 srgb_to_linear(vec3 color) {
    return mix(pow((color + 0.055) / 1.055, vec3(2.4)), color / 12.92,
        lessThanEqual(color, vec3(0.04045)));
}

void main() {
    vec4 color = In.Color * texture(sTexture, In.UV.st);
    // UI 的白色仍为 SDR 白；仅 RGB 解码，透明度不作色彩转换。
    fColor = vec4(srgb_to_linear(color.rgb), color.a);
}

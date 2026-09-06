#version 450

layout(set = 0, binding = 0) uniform sampler2D hdr_color;
layout(push_constant) uniform Parameters {
    float exposure;
    uint encode_srgb;
} parameters;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

void main() {
    vec3 hdr = max(texture(hdr_color, uv).rgb, vec3(0.0));
    vec3 mapped = vec3(1.0) - exp(-hdr * parameters.exposure);
    if(parameters.encode_srgb != 0) {
        mapped = mix(1.055 * pow(mapped, vec3(1.0 / 2.4)) - 0.055,
            12.92 * mapped, lessThanEqual(mapped, vec3(0.0031308)));
    }
    color = vec4(mapped, 1.0);
}

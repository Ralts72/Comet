#version 450

layout(location = 0) in vec2 coordinate;
layout(location = 1) in vec4 tint;
layout(location = 0) out vec4 output_color;
layout(set = 0, binding = 0) uniform sampler2D particle_texture;
layout(push_constant) uniform TextureSettings {
    layout(offset = 96) uint enabled;
} settings;

void main() {
    float falloff = 1 - smoothstep(0, 1, dot(coordinate, coordinate));
    if(settings.enabled != 0)
        output_color = tint * texture(particle_texture, coordinate * 0.5 + 0.5);
    else
        output_color = vec4(tint.rgb, tint.a * falloff);
}

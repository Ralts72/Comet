#version 450
layout(location = 0) in vec2 uv;
layout(set = 0, binding = 0) uniform sampler2D base_color_texture;
layout(push_constant) uniform Surface { float alpha; float cutoff; } surface;
void main() {
    if(clamp(surface.alpha * texture(base_color_texture, uv).a, 0.0, 1.0) < surface.cutoff)
        discard;
}

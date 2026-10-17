#version 450
layout(location=0) in vec2 uv;
layout(set=0,binding=0) uniform sampler2D base_color_texture;
layout(push_constant) uniform Outline { vec4 surface; vec4 color; vec4 style; } outline;
layout(location=0) out vec4 color;
layout(location=1) out vec4 style;
void main() {
    if(clamp(outline.surface.x * texture(base_color_texture, uv).a, 0., 1.) < outline.surface.y) discard;
    color = outline.color; style = outline.style;
}

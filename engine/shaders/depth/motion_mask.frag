#version 450
#extension GL_GOOGLE_include_directive : require
#include "../common/motion.glsl"
layout(location = 0) in vec2 uv;
layout(set = 0, binding = 0) uniform sampler2D base_color_texture;
void main() {
    if(clamp(camera.alpha * texture(base_color_texture, uv).a, 0, 1) < camera.cutoff)
        discard;
    write_motion();
}

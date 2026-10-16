#version 450
#extension GL_GOOGLE_include_directive : require
#include "../lighting/volumetric_fog.glsl"
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main() {
    color = sample_volumetric_fog_layer(fog_volume, uv, 63);
}

#version 450
#extension GL_GOOGLE_include_directive : require
#include "../common/instance_transform.glsl"
#include "../common/vertex_bend.glsl"
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
layout(location = 7) in vec4 previous_column0;
layout(location = 8) in vec4 previous_column1;
layout(location = 9) in vec4 previous_column2;
layout(location = 10) in vec4 previous_column3;
layout(location = 11) in vec3 previous_position;
layout(location = 13) in vec4 previous_bend;
layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 current_clip;
layout(location = 2) out vec4 previous_clip;
void main() {
    uv = texcoord;
    current_clip = instance_transform() * vec4(bend_position(position, instance_bend), 1);
    previous_clip = mat4(previous_column0, previous_column1, previous_column2, previous_column3)
        * vec4(bend_position(previous_position, previous_bend), 1);
    gl_Position = current_clip;
}

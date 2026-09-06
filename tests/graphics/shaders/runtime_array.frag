#version 450
#extension GL_EXT_nonuniform_qualifier : require
layout(location = 0) flat in int index;
layout(location = 0) out vec4 color;
layout(set = 1, binding = 0) uniform sampler2D images[];
void main() {
    color = texture(images[nonuniformEXT(index)], vec2(0.5));
}

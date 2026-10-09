#ifndef COMET_MESH_VERTEX_GLSL
#define COMET_MESH_VERTEX_GLSL

#include "frame.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
layout(location = 2) in vec3 normal;

#ifdef COMET_MESH_LIGHTING
layout(location = 0) out vec3 world_position;
layout(location = 1) out vec3 world_normal;
layout(location = 2) out vec2 uv;
#endif

#ifdef COMET_MESH_INSTANCED
#include "instance_transform.glsl"
#else
layout(push_constant) uniform ObjectData {
    mat4 model;
} object;
#endif

void main() {
#ifdef COMET_MESH_INSTANCED
    mat4 model = instance_transform();
#else
    mat4 model = object.model;
#endif
    vec4 world = model * vec4(position, 1.0);
    gl_Position = frame.projection * frame.view * world;
#ifdef COMET_MESH_LIGHTING
    world_position = world.xyz;
    uv = texcoord;
    mat3 basis = mat3(model);
    world_normal = vec3(0.0);
    if(abs(determinant(basis)) > 1e-8)
        world_normal = transpose(inverse(basis)) * normal;
#endif
}

#endif

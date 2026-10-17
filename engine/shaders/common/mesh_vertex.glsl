#ifndef COMET_MESH_VERTEX_GLSL
#define COMET_MESH_VERTEX_GLSL

#include "frame.glsl"
#include "vertex_bend.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
layout(location = 2) in vec3 normal;

#ifdef COMET_MESH_LIGHTING
layout(location = 7) in vec4 tangent;
layout(location = 0) out vec3 world_position;
layout(location = 1) out vec3 world_normal;
layout(location = 2) out vec2 uv;
layout(location = 3) out vec4 world_tangent;
layout(location = 4) out vec3 world_bitangent;
#endif

#ifdef COMET_MESH_INSTANCED
#include "instance_transform.glsl"
#else
layout(push_constant) uniform ObjectData {
    mat4 model;
    vec4 vertex_bend;
} object;
#endif

void main() {
#ifdef COMET_MESH_INSTANCED
    mat4 model = instance_transform();
    vec4 bend = instance_bend;
#else
    mat4 model = object.model;
    vec4 bend = object.vertex_bend;
#endif
    vec4 world = model * vec4(bend_position(position, bend), 1.0);
    gl_Position = frame.projection * frame.view * world;
#ifdef COMET_MESH_LIGHTING
    vec2 gradient = bend_gradient(position.y, bend);
    vec3 bent_normal = normal;
    bent_normal.y -= dot(gradient, normal.xz);
    vec3 bent_tangent = tangent.xyz;
    bent_tangent.xz += gradient * tangent.y;
    world_position = world.xyz;
    uv = texcoord;
    mat3 basis = mat3(model);
    world_tangent = vec4(0.0);
    world_bitangent = vec3(0.0);
    world_normal = vec3(0.0);
    float orientation = determinant(basis);
    if(abs(orientation) > 1e-8) {
        vec3 transformed_normal = transpose(inverse(basis)) * bent_normal;
        float normal_length = length(transformed_normal);
        if(normal_length > 1e-8) {
            world_normal = transformed_normal / normal_length;
            vec3 transformed_tangent = basis * bent_tangent;
            transformed_tangent -= world_normal * dot(world_normal, transformed_tangent);
            float tangent_length = length(transformed_tangent);
            if(tangent.w != 0.0 && tangent_length > 1e-8) {
                world_tangent = vec4(transformed_tangent / tangent_length, tangent.w * sign(orientation));
                world_bitangent = cross(world_normal, world_tangent.xyz) * world_tangent.w;
            }
        }
    }
#endif
}

#endif

#version 450
#extension GL_GOOGLE_include_directive : require
#include "../common/fog.glsl"
#include "../common/surface.glsl"
#include "../lighting/surface_lighting.glsl"
#include "../lighting/volumetric_fog.glsl"
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(set = 1, binding = 0) uniform sampler2D decal_color;
layout(set = 1, binding = 1) uniform sampler2D scene_depth;
layout(push_constant) uniform Decal {
    mat4 world_to_local;
    vec4 color;
    vec4 parameters; // roughness, normal threshold, edge fade
} decal;
void main() {
    vec2 depth_uv = vec2(uv.x, 1 - uv.y);
    float depth = texture(scene_depth, depth_uv).r;
    vec4 projected = lighting.inverse_view_projection * vec4(depth_uv * 2 - 1, depth, 1);
    vec3 position = projected.xyz / projected.w;
    vec3 local = (decal.world_to_local * vec4(position, 1)).xyz;
    vec3 dx = dFdx(position), dy = dFdy(position);
    vec2 uv_dx = dFdx(local.xz), uv_dy = dFdy(local.xz);
    vec3 crossed = cross(dx, dy);
    if(depth >= 1 || any(greaterThan(abs(local), vec3(.5))) || dot(crossed, crossed) < 1e-12) discard;
    vec3 normal = normalize(crossed);
    vec3 view = frame.orthographic > .5 ? frame.view_direction : frame.camera_position - position;
    view = normalize(view);
    if(dot(normal, view) < 0) normal = -normal;
    vec3 up = normalize(vec3(decal.world_to_local[0].y, decal.world_to_local[1].y, decal.world_to_local[2].y));
    if(dot(normal, up) < decal.parameters.y) discard;
    vec4 base = textureGrad(decal_color, local.xz + .5, uv_dx, uv_dy) * decal.color;
    vec3 edge = vec3(.5) - abs(local);
    float alpha = base.a;
    if(decal.parameters.z > 0) alpha *= smoothstep(0, decal.parameters.z, min(edge.x, min(edge.y, edge.z)));
    if(alpha <= 0) discard;
    vec3 radiance = evaluate_surface_lighting(position, normal, normal, view,
        clamp(base.rgb, 0, 1), 0, decal.parameters.x, 1, dx, dy);
    color = vec4(clamp(apply_volumetric_fog(apply_distance_fog_at_depth(radiance, depth), position), 0, 65504), alpha);
}

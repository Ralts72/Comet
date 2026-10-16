#ifndef COMET_VOLUMETRIC_FOG_GLSL
#define COMET_VOLUMETRIC_FOG_GLSL

// 64 depth slices packed in an 8x8 atlas; all consumers share this mapping.
vec4 fog_voxel(sampler2D atlas, ivec3 cell, ivec2 grid) {
    cell.xy = clamp(cell.xy, ivec2(0), grid - 1);
    cell.z = clamp(cell.z, 0, 63);
    ivec2 tile = ivec2(cell.z % 8, cell.z / 8);
    return texelFetch(atlas, tile * grid + cell.xy, 0);
}

vec4 sample_volumetric_fog_layer(sampler2D atlas, vec2 uv, float slice) {
    ivec2 grid = textureSize(atlas, 0) / 8;
    vec3 cell = clamp(vec3(uv * vec2(grid) - .5, slice), vec3(0), vec3(vec2(grid - 1), 63));
    ivec3 base = ivec3(floor(cell));
    vec3 fraction = fract(cell);
    vec4 a = mix(fog_voxel(atlas, base, grid), fog_voxel(atlas, base + ivec3(1, 0, 0), grid), fraction.x);
    vec4 b = mix(fog_voxel(atlas, base + ivec3(0, 1, 0), grid), fog_voxel(atlas, base + ivec3(1, 1, 0), grid), fraction.x);
    vec4 c = mix(fog_voxel(atlas, base + ivec3(0, 0, 1), grid), fog_voxel(atlas, base + ivec3(1, 0, 1), grid), fraction.x);
    vec4 d = mix(fog_voxel(atlas, base + ivec3(0, 1, 1), grid), fog_voxel(atlas, base + ivec3(1, 1, 1), grid), fraction.x);
    return mix(mix(a, b, fraction.y), mix(c, d, fraction.y), fraction.z);
}
vec4 sample_volumetric_fog(sampler2D atlas, vec2 range, vec2 uv, float distance) {
    if(range.x <= 0) return vec4(0, 0, 0, 1);
    float slice = log(max(distance, range.x) / range.x) * range.y * 64 - 1;
    return sample_volumetric_fog_layer(atlas, uv, slice);
}

#endif

layout(set = 0, binding = 6) uniform sampler2D ao_depth;

// Fixed disk kernel in camera depth. Missing/offscreen geometry contributes no occlusion.
float ambient_visibility(vec3 position, vec3 normal) {
    float radius = lighting.ao_parameters.x;
    if(radius <= 0.0 || (lighting.environment.x <= 0.0 && lighting.reflection_metadata.x <= 0.0
        && lighting.irradiance_origin.w <= 0.0) || comet_alpha_mode == 2)
        return 1.0;
    vec4 clip = frame.projection * frame.view * vec4(position, 1.0);
    if(clip.w <= 0.0)
        return 1.0;
    vec2 center = clip.xy / clip.w * 0.5 + 0.5;
    vec2 extent = abs(vec2(frame.projection[0][0], frame.projection[1][1])) * radius / (2.0 * clip.w);
    float obscured = 0.0;
    const int count = 16;
    for(int i = 0; i < count; ++i) {
        float angle = float(i) * 2.39996323;
        float ring = sqrt((float(i) + 0.5) / float(count));
        vec2 sample_uv = center + vec2(cos(angle), sin(angle)) * ring * extent;
        if(any(lessThan(sample_uv, vec2(0.0))) || any(greaterThan(sample_uv, vec2(1.0))))
            continue;
        ivec2 size = textureSize(ao_depth, 0);
        ivec2 pixel = clamp(ivec2(sample_uv * vec2(size)), ivec2(0), size - 1);
        float depth = texelFetch(ao_depth, pixel, 0).r;
        if(depth >= 1.0)
            continue;
        // Reconstruct at the fetched texel center, not at the continuous kernel offset.
        vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
        vec4 world = lighting.ao_inverse_view_projection * vec4(uv * 2.0 - 1.0, depth, 1.0);
        if(abs(world.w) < 1e-8)
            continue;
        vec3 delta = world.xyz / world.w - position;
        float distance_squared = dot(delta, delta);
        if(distance_squared <= 1e-8 || distance_squared >= radius * radius)
            continue;
        float elevation = max(dot(normal, delta) - radius * 0.02, 0.0) / sqrt(distance_squared);
        obscured += elevation * (1.0 - distance_squared / (radius * radius));
    }
    return clamp(1.0 - lighting.ao_parameters.y * 3.0 * obscured / float(count), 0.0, 1.0);
}

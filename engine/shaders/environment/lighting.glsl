layout(set = 0, binding = 3) uniform samplerCube environment_irradiance;
layout(set = 0, binding = 4) uniform samplerCube environment_specular;
layout(set = 0, binding = 5) uniform sampler2D environment_brdf;
layout(set = 0, binding = 7) uniform samplerCube reflection_probe0;
layout(set = 0, binding = 8) uniform samplerCube reflection_probe1;
layout(set = 0, binding = 9) uniform samplerCube reflection_probe2;
layout(set = 0, binding = 10) uniform samplerCube reflection_probe3;

vec3 sample_reflection_probe(int index, vec3 direction, float lod) {
    if(index == 0) return textureLod(reflection_probe0, direction, lod).rgb;
    if(index == 1) return textureLod(reflection_probe1, direction, lod).rgb;
    if(index == 2) return textureLod(reflection_probe2, direction, lod).rgb;
    return textureLod(reflection_probe3, direction, lod).rgb;
}

vec3 local_reflection(vec3 position, vec3 reflection, float roughness, vec3 global) {
    vec3 local = vec3(0.0);
    float total_weight = 0.0;
    for(int index = 0; index < int(lighting.reflection_metadata.x); ++index) {
        ReflectionProbe probe = lighting.reflection_probes[index];
        vec3 edge = min(position - probe.minimum_blend.xyz, probe.maximum_lod.xyz - position);
        float distance = min(edge.x, min(edge.y, edge.z));
        if(distance < 0.0) continue;
        float weight = probe.minimum_blend.w > 0.0 ? clamp(distance / probe.minimum_blend.w, 0.0, 1.0) : 1.0;
        if(weight <= 0.0) continue;
        vec3 direction = reflection;
        if(probe.rotation_projection.z > 0.5) {
            vec3 boundary = mix(probe.minimum_blend.xyz, probe.maximum_lod.xyz, greaterThanEqual(reflection, vec3(0.0)));
            vec3 steps;
            for(int axis = 0; axis < 3; ++axis)
                steps[axis] = abs(reflection[axis]) > 1e-6 ? (boundary[axis] - position[axis]) / reflection[axis] : 1e30;
            float hit = min(steps.x, min(steps.y, steps.z));
            direction = position + reflection * hit - probe.position_intensity.xyz;
        }
        float sine = probe.rotation_projection.x, cosine = probe.rotation_projection.y;
        direction = vec3(cosine * direction.x - sine * direction.z, direction.y,
            sine * direction.x + cosine * direction.z);
        local += weight * probe.position_intensity.w * sample_reflection_probe(index, direction, roughness * probe.maximum_lod.w);
        total_weight += weight;
    }
    return (global * max(1.0 - total_weight, 0.0) + local) / max(total_weight, 1.0);
}

vec3 environment_direction(vec3 direction) {
    float sine = lighting.environment.z;
    float cosine = lighting.environment.w;
    return vec3(cosine * direction.x - sine * direction.z, direction.y,
        sine * direction.x + cosine * direction.z);
}

vec3 environment_reflected_energy(float n_dot_v, vec3 base_color, float metallic, float roughness) {
    vec3 f0 = mix(vec3(0.04), base_color, metallic);
    vec3 reflected_energy = f0 + (1.0 - f0) * pow(1.0 - n_dot_v, 5.0);
    if(lighting.environment.x > 0.0 || lighting.reflection_metadata.x > 0.0) {
        vec2 brdf = texture(environment_brdf, vec2(n_dot_v, roughness)).rg;
        reflected_energy = f0 * brdf.x + brdf.y;
    }
    return reflected_energy;
}

vec3 environment_specular_radiance(vec3 position, vec3 normal, vec3 view, float roughness) {
    vec3 reflection = environment_direction(reflect(-view, normal));
    vec3 specular = textureLod(environment_specular, reflection,
        roughness * lighting.environment.y).rgb;
    return local_reflection(position, reflect(-view, normal), roughness,
        specular * lighting.environment.x);
}

vec3 evaluate_environment(vec3 position, vec3 normal, vec3 view, vec3 base_color,
    float metallic, float roughness) {
    if(lighting.environment.x <= 0.0 && lighting.reflection_metadata.x <= 0.0
        && lighting.irradiance_metadata.x <= 0.0)
        return vec3(0.0);
    float n_dot_v = clamp(dot(normal, view), 0.0, 1.0);
    if(n_dot_v <= 0.0)
        return vec3(0.0);
    vec3 reflected_energy = environment_reflected_energy(n_dot_v, base_color, metallic, roughness);
    // Irradiance stores E/pi; the LUT partitions single-scattering specular and diffuse energy.
    vec3 irradiance;
    if(!sample_irradiance(position, normal, irradiance))
        irradiance = texture(environment_irradiance, environment_direction(normal)).rgb * lighting.environment.x;
    vec3 diffuse = irradiance * base_color * (1.0 - metallic) * (1.0 - reflected_energy);
    vec3 indirect_specular = environment_specular_radiance(position, normal, view, roughness)
        * reflected_energy;
    return diffuse + indirect_specular;
}

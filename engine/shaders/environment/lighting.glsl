layout(set = 0, binding = 3) uniform samplerCube environment_irradiance;
layout(set = 0, binding = 4) uniform samplerCube environment_specular;
layout(set = 0, binding = 5) uniform sampler2D environment_brdf;

vec3 environment_direction(vec3 direction) {
    float sine = lighting.environment.z;
    float cosine = lighting.environment.w;
    return vec3(cosine * direction.x - sine * direction.z, direction.y,
        sine * direction.x + cosine * direction.z);
}

vec3 evaluate_environment(vec3 normal, vec3 view, vec3 base_color,
    float metallic, float roughness) {
    if(lighting.environment.x <= 0.0)
        return vec3(0.0);
    float n_dot_v = clamp(dot(normal, view), 0.0, 1.0);
    if(n_dot_v <= 0.0)
        return vec3(0.0);
    vec3 f0 = mix(vec3(0.04), base_color, metallic);
    vec2 brdf = texture(environment_brdf, vec2(n_dot_v, roughness)).rg;
    vec3 reflected_energy = f0 * brdf.x + brdf.y;
    // Irradiance stores E/pi; the LUT partitions single-scattering specular and diffuse energy.
    vec3 diffuse = texture(environment_irradiance, environment_direction(normal)).rgb
        * base_color * (1.0 - metallic) * (1.0 - reflected_energy);
    vec3 reflection = environment_direction(reflect(-view, normal));
    vec3 specular = textureLod(environment_specular, reflection,
        roughness * lighting.environment.y).rgb * reflected_energy;
    return (diffuse + specular) * lighting.environment.x;
}

void irradiance_basis(vec3 direction, out float basis[9]) {
    basis = float[9](0.2820947918,
        0.4886025119 * direction.y, 0.4886025119 * direction.z, 0.4886025119 * direction.x,
        1.0925484306 * direction.x * direction.y, 1.0925484306 * direction.y * direction.z,
        0.3153915653 * (3.0 * direction.z * direction.z - 1.0),
        1.0925484306 * direction.x * direction.z,
        0.5462742153 * (direction.x * direction.x - direction.y * direction.y));
}

vec3 irradiance_oct_direction(vec2 coordinate) {
    vec2 p = coordinate * 2.0 - 1.0;
    vec3 direction = vec3(p, 1.0 - abs(p.x) - abs(p.y));
    if(direction.z < 0.0) {
        vec2 signs = mix(vec2(1.0), vec2(-1.0), lessThan(p, vec2(0.0)));
        direction.xy = (1.0 - abs(p.yx)) * signs;
    }
    return normalize(direction);
}

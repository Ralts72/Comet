#ifndef COMET_WATER_GLSL
#define COMET_WATER_GLSL
vec3 water_normal(vec3 normal, vec3 point, vec4 wave) {
    if(wave.w <= 0 || wave.x <= 0) return normal;
    float a = (point.x + point.z * .45) * wave.y + wave.z;
    float b = (point.x * .7 - point.z * 1.1) * wave.y + wave.z * .7;
    vec2 slope = wave.x * wave.y * (vec2(1, .45) * cos(a) + vec2(.35, -.55) * cos(b));
    return normalize(normal - vec3(slope.x, 0, slope.y) * normal.y);
}
#endif

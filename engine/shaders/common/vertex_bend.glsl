#ifndef COMET_VERTEX_BEND_GLSL
#define COMET_VERTEX_BEND_GLSL
// X/Z 顶部偏移系数与本地弯曲高度；根部固定，超过顶部延续偏移。
vec2 bend_gradient(float y, vec4 bend) {
    if(bend.z <= 0 || y <= 0 || y >= bend.z) return vec2(0);
    return 2 * bend.xy * y / bend.z;
}
vec3 bend_position(vec3 point, vec4 bend) {
    if(bend.z <= 0) return point;
    float height = clamp(point.y / bend.z, 0, 1);
    point.xz += bend.xy * bend.z * height * height;
    return point;
}
#endif

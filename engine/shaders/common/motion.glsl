layout(location = 1) in vec4 current_clip;
layout(location = 2) in vec4 previous_clip;
layout(location = 0) out vec4 motion;
layout(push_constant) uniform Camera {
    float alpha;
    float cutoff;
    layout(offset = 16) vec4 projection;
} camera;
void write_motion() {
    if(previous_clip.w <= 0 || current_clip.w <= 0) {
        motion = vec4(0);
        return;
    }
    vec3 previous = previous_clip.xyz / previous_clip.w;
    vec2 delta = current_clip.xy / current_clip.w - previous.xy;
    float denominator = previous.z * camera.projection.z - camera.projection.x;
    float distance = abs((previous.z * camera.projection.w - camera.projection.y) / denominator);
    // Scene color uses a negative-height viewport; depth uses a positive-height viewport.
    motion = vec4(delta * vec2(.5, -.5), min(distance, 65504), 1);
}

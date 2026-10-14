#ifndef COMET_SURFACE_GLSL
#define COMET_SURFACE_GLSL

layout(constant_id = 100) const uint comet_alpha_mode = 0;
layout(constant_id = 101) const float comet_alpha_cutoff = 0.5;
layout(constant_id = 102) const bool comet_double_sided = false;

float surface_alpha(float alpha) {
    alpha = clamp(alpha, 0.0, 1.0);
    if(comet_alpha_mode == 1 && alpha < comet_alpha_cutoff)
        discard;
    return comet_alpha_mode == 2 ? alpha : 1.0;
}

#endif

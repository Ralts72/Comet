#version 450
layout(set=0,binding=0) uniform sampler2D scene;
layout(set=0,binding=1) uniform sampler2D accumulation;
layout(set=0,binding=2) uniform sampler2D transmission;
layout(location=0) out vec4 color;
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec4 background=texelFetch(scene,pixel,0);
    vec4 sum=texelFetch(accumulation,pixel,0);
    vec4 optical=texelFetch(transmission,pixel,0);
    float reveal=exp(-optical.a);
    vec3 blended=sum.rgb/max(sum.a,1e-5);
    // 加法光不吸收背景，但运动时也需要降低 TAA 历史权重。
    float additive_coverage=clamp(max(optical.r,max(optical.g,optical.b)),0.0,1.0);
    color=vec4(blended*(1-reveal)+background.rgb*reveal+optical.rgb,
        max(background.a,max(1-reveal,additive_coverage)));
}

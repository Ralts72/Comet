// McGuire / Bavoil, JCGT 2013. 对数透射率允许两个 MRT 使用相同加法混合。
layout(set=2,binding=0) uniform texture2D comet_oit_depth;
layout(location=1) out vec4 comet_oit_transmission;
void accumulate_transparency(inout vec4 color, bool additive) {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    pixel.y=textureSize(comet_oit_depth,0).y-1-pixel.y;
    if(gl_FragCoord.z>texelFetch(comet_oit_depth,pixel,0).r+1e-6) discard;
    float alpha=clamp(color.a,0.0,1.0);
    if(additive) {
        comet_oit_transmission=vec4(color.rgb*alpha,0);
        color=vec4(0);
    } else {
        float weight=clamp(pow(alpha+.01,3.0)*8.0*pow(1.0-.9*gl_FragCoord.z,3.0),.01,8.0);
        comet_oit_transmission=vec4(0,0,0,-log(max(1.0-alpha,1e-5)));
        color=vec4(color.rgb*alpha,alpha)*weight;
    }
}

#version 450
layout(set=0,binding=0) uniform texture2D scene_image;
layout(set=0,binding=1) uniform texture2D color_image;
layout(set=0,binding=2) uniform texture2D style_image;
layout(set=0,binding=3) uniform texture2D selected_depth_image;
layout(set=0,binding=4) uniform texture2D scene_depth_image;
layout(set=0,binding=5) uniform sampler point_sampler;
layout(push_constant) uniform Parameters { vec4 scale; } parameters;
layout(location=0) out vec4 color;
#define IMAGE(name) sampler2D(name, point_sampler)
const ivec2 directions[8] = ivec2[8](ivec2(1,0),ivec2(-1,0),ivec2(0,1),ivec2(0,-1),
    ivec2(1,1),ivec2(-1,1),ivec2(1,-1),ivec2(-1,-1));
bool present(ivec2 p, ivec2 size, float foreground, out vec4 tint, out vec4 style) {
    if(any(lessThan(p,ivec2(0))) || any(greaterThanEqual(p,size))) return false;
    // 几何深度与遮罩使用正高度 viewport，场景颜色使用负高度 viewport。
    p.y = size.y - 1 - p.y;
    tint = texelFetch(IMAGE(color_image),p,0);
    if(tint.a<=0) return false;
    style = texelFetch(IMAGE(style_image),p,0);
    if(style.z>.5) return true;
    float depth = texelFetch(IMAGE(selected_depth_image),p,0).r;
    return depth <= foreground + 1e-5;
}
void main() {
    ivec2 p=ivec2(gl_FragCoord.xy), size=textureSize(IMAGE(scene_image),0);
    color=texelFetch(IMAGE(scene_image),p,0);
    float depth=texelFetch(IMAGE(scene_depth_image),ivec2(p.x,size.y-1-p.y),0).r;
    vec4 tint, style;
    if(present(p,size,depth,tint,style)) {
        color.rgb=mix(color.rgb,tint.rgb,tint.a*style.y);
        return;
    }
    // 宽度以输出像素计，内部渲染比例变化时保持相同的视觉尺寸。
    for(int radius=1; radius<=int(ceil(parameters.scale.z)); ++radius) {
        for(int i=0; i<8; ++i) {
            ivec2 offset=ivec2(round(vec2(directions[i])*float(radius)*parameters.scale.xy));
            if(offset==ivec2(0)) continue;
            ivec2 q=p+offset;
            if(present(q,size,depth,tint,style) && float(radius)<style.x+1.0) {
                color.rgb=mix(color.rgb,tint.rgb,tint.a*clamp(style.x-float(radius)+1.0,0.0,1.0));
                return;
            }
        }
    }
}

#version 450
#extension GL_GOOGLE_include_directive : require
#include "post_process.glsl"

layout(set = 0, binding = 0) uniform sampler2D input_color;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

void main() {
    ivec2 size = textureSize(input_color, 0);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec3 result = vec3(0.0);
    if(parameters.mode == 0) {
        // 先在源像素提取再平均，保留亚像素高亮；奇数边缘只计有效样本。
        float count = 0.0;
        for(int y = 0; y < 2; ++y)
            for(int x = 0; x < 2; ++x) {
                ivec2 source = pixel * 2 + ivec2(x, y);
                if(any(greaterThanEqual(source, size)))
                    continue;
                vec3 value = clamp(texelFetch(input_color, source, 0).rgb, 0.0, 65504.0);
                float brightness = max(value.r, max(value.g, value.b));
                result += value * max(brightness - parameters.bloom_threshold, 0.0)
                    / max(brightness, 1e-5);
                count += 1.0;
            }
        result /= max(count, 1.0);
    } else {
        // 归一化、可分离的九 tap 二项核；nearest/texelFetch 不依赖 HDR 线性过滤。
        const float weights[5] = float[](70.0, 56.0, 28.0, 8.0, 1.0);
        ivec2 direction = parameters.mode == 1 ? ivec2(1, 0) : ivec2(0, 1);
        for(int offset = -4; offset <= 4; ++offset) {
            ivec2 source = clamp(pixel + direction * offset, ivec2(0), size - 1);
            result += texelFetch(input_color, source, 0).rgb * weights[abs(offset)] / 256.0;
        }
    }
    color = vec4(clamp(result, 0.0, 65504.0), 1.0);
}

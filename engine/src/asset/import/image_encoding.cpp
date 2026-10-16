#include "asset/import/image_encoding.h"
#include "asset/data/texture_sampling.h"

#include <stb_image_write.h>
#include <algorithm>
#include <cmath>

namespace Comet {
    Result<std::string> encode_image(
        const TextureData& image, std::string_view extension, float exposure) {
        using Encoded = Result<std::string>;
        if(extension != ".png" && extension != ".hdr")
            return Encoded::failure("图像格式仅支持 .png 或 .hdr");
        if(extension == ".png" && (!std::isfinite(exposure) || exposure < 0))
            return Encoded::failure("图像曝光必须为有限非负数");
        auto decoded = CpuTextureImage::decode(image, 128 * 1024 * 1024);
        if(!decoded)
            return Encoded::failure(decoded.error());
        auto pixels = std::move(decoded).value();
        std::string encoded;
        const auto append = [](void* target, void* data, int bytes) {
            static_cast<std::string*>(target)->append(
                static_cast<const char*>(data), std::size_t(bytes));
        };
        int written = 0;
        if(extension == ".hdr") {
            // stb 的 HDR 写入接受浮点 RGB(A)，忽略 alpha，不应用曝光或色调映射。
            written = stbi_write_hdr_to_func(append, &encoded, image.width, image.height, 4,
                reinterpret_cast<const float*>(pixels.pixels.data()));
        } else {
            std::vector<uint8_t> display(pixels.pixels.size() * 4);
            for(std::size_t i = 0; i < pixels.pixels.size(); ++i) {
                for(int channel = 0; channel < 3; ++channel) {
                    // 与 SDR display.frag 一致：先曝光／指数映射，再编码为 sRGB。
                    const float mapped =
                        1 - std::exp(-std::min(pixels.pixels[i][channel], 65504.f) * exposure);
                    const float srgb = mapped <= .0031308f
                                           ? mapped * 12.92f
                                           : 1.055f * std::pow(mapped, 1.f / 2.4f) - .055f;
                    display[i * 4 + channel] =
                        static_cast<uint8_t>(std::lround(std::clamp(srgb, 0.f, 1.f) * 255));
                }
                display[i * 4 + 3] = 255;
            }
            written = stbi_write_png_to_func(
                append, &encoded, image.width, image.height, 4, display.data(), image.width * 4);
        }
        if(!written)
            return Encoded::failure("图像编码失败");
        return Encoded::success(std::move(encoded));
    }
}

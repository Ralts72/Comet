#pragma once

#include <array>
#include <filesystem>
#include <fstream>
#include <functional>

namespace Comet::Tests {
    // 生成未压缩 Radiance RGBE，让测试覆盖真实 HDR 解码而不依赖外部资源文件。
    inline void write_hdr(
        const std::filesystem::path& path, int width = 16, int height = 8,
        const std::function<std::array<unsigned char, 4>(int, int)>& pixel = [](int, int) {
            return std::array<unsigned char, 4>{128, 64, 32, 131};
        }) {
        std::ofstream output(path, std::ios::binary);
        output << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " << height << " +X " << width << '\n';
        for(int y = 0; y < height; ++y)
            for(int x = 0; x < width; ++x) {
                const auto value = pixel(x, y);
                output.write(reinterpret_cast<const char*>(value.data()), value.size());
            }
    }
}

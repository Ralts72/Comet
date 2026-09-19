#include "asset/import/environment_importer.h"

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <numbers>

namespace Comet {
    static Result<std::size_t> estimate_bytes(
        const std::size_t source_size, const int width, const int height) {
        if(source_size > 256 * 1024 * 1024 || width < 4 || width > 8192 || height < 2
            || height > 4096 || width != height * 2)
            return Result<std::size_t>::failure(
                "Environment requires a 2:1 HDR up to 8K / 256 MiB");
        const auto face = std::bit_floor(static_cast<std::size_t>(width / 4));
        // Decode + float faces/reduction + mip vector growth and artifact serialization.
        return Result<std::size_t>::success(source_size + std::size_t(width) * height * 16
                                            + face * face * 6 * 20 + face * face * 6 * 8 * 4);
    }

    Result<std::size_t> EnvironmentImporter::working_bytes(const std::filesystem::path& path) {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        int width = 0, height = 0, channels = 0;
        if(error || !stbi_info(path.string().c_str(), &width, &height, &channels))
            return Result<std::size_t>::failure(
                "Cannot inspect environment source: " + path.string());
        return estimate_bytes(size, width, height);
    }

    // stb's HDR decoder ignores EOF in flat pixels and the last RLE run. Check framing first.
    static bool complete_hdr_payload(
        const std::string_view source, const int width, const int height) {
        const auto header_end = source.find("\n\n");
        if(header_end == std::string_view::npos)
            return false;
        const auto resolution_end = source.find('\n', header_end + 2);
        if(resolution_end == std::string_view::npos)
            return false;
        size_t cursor = resolution_end + 1;
        const auto byte = [&](size_t offset) { return static_cast<unsigned char>(source[offset]); };
        if(source.size() - cursor < 4)
            return false;
        if(width < 8 || byte(cursor) != 2 || byte(cursor + 1) != 2 || (byte(cursor + 2) & 128))
            return source.size() - cursor >= size_t(width) * height * 4;
        for(int y = 0; y < height; ++y) {
            if(source.size() - cursor < 4 || byte(cursor) != 2 || byte(cursor + 1) != 2
                || ((int(byte(cursor + 2)) << 8) | byte(cursor + 3)) != width)
                return false;
            cursor += 4;
            for(int channel = 0; channel < 4; ++channel) {
                int remaining = width;
                while(remaining > 0) {
                    if(cursor == source.size())
                        return false;
                    const auto count = byte(cursor++);
                    const int decoded = count > 128 ? count - 128 : count;
                    const size_t encoded = count > 128 ? 1 : count;
                    if(decoded == 0 || decoded > remaining || source.size() - cursor < encoded)
                        return false;
                    remaining -= decoded;
                    cursor += encoded;
                }
            }
        }
        return true;
    }

    Result<TextureData> EnvironmentImporter::import(
        const std::filesystem::path& source_path, const std::size_t memory_budget) const {
        const auto path = source_path.string();
        std::error_code error;
        const auto size_on_disk = std::filesystem::file_size(source_path, error);
        if(error || size_on_disk > 256 * 1024 * 1024 || size_on_disk > memory_budget)
            return Result<TextureData>::failure(
                "Cannot read HDR environment or source exceeds 256 MiB: " + path);
        std::string source(size_on_disk, '\0');
        std::ifstream input(source_path, std::ios::binary);
        if(!input.read(source.data(), source.size())
            || input.peek() != std::ifstream::traits_type::eof())
            return Result<TextureData>::failure("Cannot read HDR or source size changed: " + path);
        const auto* bytes = reinterpret_cast<const stbi_uc*>(source.data());
        const int byte_count = static_cast<int>(source.size());
        int width = 0, height = 0, channels = 0;
        if(!stbi_is_hdr_from_memory(bytes, byte_count)
            || !stbi_info_from_memory(bytes, byte_count, &width, &height, &channels) || width < 4
            || width > 8192 || height < 2 || height > 4096 || width != height * 2)
            return Result<TextureData>::failure(
                "Environment requires a 2:1 Radiance HDR image (4..8192 pixels wide)");
        if(!complete_hdr_payload(source, width, height))
            return Result<TextureData>::failure("Truncated or invalid HDR pixel stream: " + path);
        const auto required = estimate_bytes(source.size(), width, height);
        if(!required || required.value() > memory_budget)
            return Result<TextureData>::failure(
                "Environment exceeds its reserved CPU memory budget");
        std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
            stbi_loadf_from_memory(bytes, byte_count, &width, &height, &channels, 4),
            &stbi_image_free);
        if(!pixels)
            return Result<TextureData>::failure("Failed to decode HDR environment: " + path);
        for(size_t i = 0; i < size_t(width) * height * 4; ++i) {
            if(!std::isfinite(pixels.get()[i]) || pixels.get()[i] < 0 || pixels.get()[i] > 65504.0f)
                return Result<TextureData>::failure(
                    "HDR pixels must be finite, nonnegative and representable as float16");
        }

        const int size = static_cast<int>(std::bit_floor(std::min(uint32_t(width / 4), 2048u)));
        TextureData result{.width = size,
            .height = size,
            .format = Format::R16G16B16A16_SFLOAT,
            .mip_levels = static_cast<uint32_t>(std::bit_width(static_cast<uint32_t>(size))),
            .cubemap = true};
        std::vector<glm::vec4> level(size_t(size) * size * 6);
        const auto texel = [&](int x, int y) {
            x = (x % width + width) % width;
            y = std::clamp(y, 0, height - 1);
            const float* p = pixels.get() + (size_t(y) * width + x) * 4;
            return glm::vec4(p[0], p[1], p[2], 1.0f);
        };
        for(int face = 0; face < 6; ++face) {
            for(int y = 0; y < size; ++y) {
                for(int x = 0; x < size; ++x) {
                    const float u = 2.0f * (float(x) + 0.5f) / float(size) - 1.0f;
                    const float v = 2.0f * (float(y) + 0.5f) / float(size) - 1.0f;
                    const std::array<glm::vec3, 6> directions{{{1, -v, -u}, {-1, -v, u}, {u, 1, v},
                        {u, -1, -v}, {u, -v, 1}, {-u, -v, -1}}};
                    const auto direction = glm::normalize(directions[face]);
                    const float longitude = std::atan2(direction.z, direction.x);
                    const float latitude = std::acos(std::clamp(direction.y, -1.0f, 1.0f));
                    const float px =
                        (longitude / (2 * std::numbers::pi_v<float>)+0.5f) * width - 0.5f;
                    const float py = latitude / std::numbers::pi_v<float> * height - 0.5f;
                    const int ix = static_cast<int>(std::floor(px));
                    const int iy = static_cast<int>(std::floor(py));
                    level[(size_t(face) * size + y) * size + x] =
                        glm::mix(glm::mix(texel(ix, iy), texel(ix + 1, iy), px - ix),
                            glm::mix(texel(ix, iy + 1), texel(ix + 1, iy + 1), px - ix), py - iy);
                }
            }
        }
        // Background mip chain only; roughness-prefiltered lighting is a separate IBL artifact.
        for(int extent = size; extent > 0; extent /= 2) {
            const size_t offset = result.pixels.size();
            result.pixels.resize(offset + level.size() * sizeof(glm::u16vec4));
            for(size_t i = 0; i < level.size(); ++i) {
                const auto packed = glm::packHalf(level[i]);
                std::memcpy(
                    result.pixels.data() + offset + i * sizeof(packed), &packed, sizeof(packed));
            }
            if(extent == 1)
                break;
            const int next = extent / 2;
            std::vector<glm::vec4> reduced(size_t(next) * next * 6);
            for(int face = 0; face < 6; ++face)
                for(int y = 0; y < next; ++y)
                    for(int x = 0; x < next; ++x) {
                        const size_t source = (size_t(face) * extent + y * 2) * extent + x * 2;
                        reduced[(size_t(face) * next + y) * next + x] =
                            (level[source] + level[source + 1] + level[source + extent]
                                + level[source + extent + 1])
                            * 0.25f;
                    }
            level = std::move(reduced);
        }
        return Result<TextureData>::success(std::move(result));
    }
}

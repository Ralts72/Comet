#include "core/math_utils.h"
#include "asset/import/environment_importer.h"
#include "asset/data/cubemap_sampling.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <numbers>

namespace Comet {
    static constexpr float PI = std::numbers::pi_v<float>;
    static constexpr uint32_t SAMPLE_COUNT = 256;

    static Math::Vec2 hammersley(uint32_t index) {
        const float x = float(index) / SAMPLE_COUNT;
        index = (index << 16) | (index >> 16);
        index = ((index & 0x55555555u) << 1) | ((index & 0xAAAAAAAAu) >> 1);
        index = ((index & 0x33333333u) << 2) | ((index & 0xCCCCCCCCu) >> 2);
        index = ((index & 0x0F0F0F0Fu) << 4) | ((index & 0xF0F0F0F0u) >> 4);
        index = ((index & 0x00FF00FFu) << 8) | ((index & 0xFF00FF00u) >> 8);
        return {x, float(index) * 2.3283064365386963e-10f};
    }

    static Math::Vec3 hemisphere(float azimuth, float cosine) {
        const float sine = std::sqrt(std::max(0.0f, 1 - cosine * cosine));
        return {sine * std::cos(azimuth), sine * std::sin(azimuth), cosine};
    }

    static Math::Vec3 sample_ggx(Math::Vec2 xi, float roughness) {
        const float alpha = roughness * roughness;
        const float cosine = std::sqrt((1 - xi.y) / (1 + (alpha * alpha - 1) * xi.y));
        return hemisphere(2 * PI * xi.x, cosine);
    }

    static void append_pixel(TextureData& data, Math::Vec3 pixel) {
        const auto packed = Math::pack_half(Math::Vec4(Math::clamp(pixel, 0.0f, 65504.0f), 1));
        const auto offset = data.pixels.size();
        data.pixels.resize(offset + sizeof(packed));
        std::memcpy(data.pixels.data() + offset, &packed, sizeof(packed));
    }

    static TextureData make_texture(int size, bool cube, bool mips) {
        TextureData result{.width = size,
            .height = size,
            .format = Format::R16G16B16A16_SFLOAT,
            .mip_levels = 1,
            .cubemap = cube};
        if(mips)
            result.mip_levels = std::bit_width(static_cast<uint32_t>(size));
        return result;
    }

    // GGX 分离求和使用 alpha = roughness² 和高度相关 Smith 可见性，与直接光照的 BRDF 一致。
    // 在纹素中心积分，避开掠射角奇点。
    static TextureData integrate_brdf() {
        auto result = make_texture(EnvironmentData::BRDF_SIZE, false, false);
        for(int y = 0; y < result.height; ++y) {
            const float roughness = (y + 0.5f) / result.height;
            const float a2 = std::pow(roughness, 4.0f);
            for(int x = 0; x < result.width; ++x) {
                const float nv = (x + 0.5f) / result.width;
                const Math::Vec3 v(std::sqrt(1 - nv * nv), 0, nv);
                Math::Vec3 sum(0);
                for(uint32_t i = 0; i < SAMPLE_COUNT; ++i) {
                    const auto h = sample_ggx(hammersley(i), roughness);
                    const float vh = std::max(Math::dot(v, h), 0.0f);
                    const auto l = 2 * vh * h - v;
                    if(l.z <= 0)
                        continue;
                    const float visibility = 0.5f
                                             / (nv * std::sqrt(a2 + (1 - a2) * l.z * l.z)
                                                 + l.z * std::sqrt(a2 + (1 - a2) * nv * nv));
                    const float weight = 4 * visibility * l.z * vh / h.z;
                    const float fresnel = std::pow(1 - vh, 5.0f);
                    sum += Math::Vec3((1 - fresnel) * weight, fresnel * weight, 0);
                }
                append_pixel(result, sum / float(SAMPLE_COUNT));
            }
        }
        return result;
    }

    static TextureData convolve(const TextureData& background, bool specular) {
        const int limit =
            specular ? EnvironmentData::SPECULAR_SIZE : EnvironmentData::IRRADIANCE_SIZE;
        auto result = make_texture(std::min(background.width, limit), true, specular);
        CpuCubeSampler source(background);
        const float texel_angle = 4 * PI / (6 * background.width * background.width);
        for(uint32_t mip = 0; mip < result.mip_levels; ++mip) {
            const int size = result.width >> mip;
            float roughness = 0;
            if(result.mip_levels > 1)
                roughness = float(mip) / float(result.mip_levels - 1);
            struct FilterSample {
                Math::Vec3 direction;
                float weight;
                float lod;
            };
            std::vector<FilterSample> samples;
            samples.reserve(SAMPLE_COUNT);
            float weight_sum = 0;
            for(uint32_t i = 0; i < SAMPLE_COUNT; ++i) {
                const auto xi = hammersley(i);
                auto direction = hemisphere(2 * PI * xi.x, std::sqrt(1 - xi.y));
                float weight = 1;
                float pdf = direction.z / PI;
                if(specular) {
                    if(mip == 0)
                        break;
                    const auto h = sample_ggx(xi, roughness);
                    direction = 2 * h.z * h - Math::Vec3(0, 0, 1);
                    weight = std::max(direction.z, 0.0f);
                    const float a2 = std::pow(roughness, 4.0f);
                    const float d = 1 - h.z * h.z + a2 * h.z * h.z;
                    pdf = a2 / (4 * PI * d * d);
                }
                if(weight <= 0)
                    continue;
                const float sample_angle = 1 / (SAMPLE_COUNT * std::max(pdf, 1e-6f));
                samples.push_back(
                    {direction, weight, 0.5f * std::log2(sample_angle / texel_angle)});
                weight_sum += weight;
            }
            for(int face = 0; face < 6; ++face)
                for(int y = 0; y < size; ++y)
                    for(int x = 0; x < size; ++x) {
                        const auto n = CpuCubeSampler::face_direction(
                            face, 2.0f * (x + 0.5f) / size - 1, 2.0f * (y + 0.5f) / size - 1);
                        if(specular && mip == 0) {
                            append_pixel(result,
                                source.sample(n, std::log2(float(background.width) / size)));
                            continue;
                        }
                        Math::Vec3 up(0, 0, 1);
                        if(std::abs(n.z) > 0.999f)
                            up = {1, 0, 0};
                        const auto tangent = Math::normalize(Math::cross(up, n));
                        const auto bitangent = Math::cross(n, tangent);
                        Math::Vec3 sum(0);
                        for(const auto& sample : samples) {
                            const auto& local = sample.direction;
                            const auto l = tangent * local.x + bitangent * local.y + n * local.z;
                            sum += source.sample(l, sample.lod) * sample.weight;
                        }
                        append_pixel(result, sum / weight_sum);
                    }
        }
        return result;
    }

    Result<std::string> EnvironmentImporter::encode_hdr(const TextureData& cubemap) {
        using Encoded = Result<std::string>;
        if(!cubemap.cubemap || cubemap.format != Format::R16G16B16A16_SFLOAT || cubemap.width < 1
            || cubemap.width > 2048 || cubemap.width != cubemap.height
            || !std::has_single_bit(static_cast<uint32_t>(cubemap.width)) || cubemap.mip_levels != 1
            || cubemap.pixels.size() != std::size_t(cubemap.width) * cubemap.height * 6 * 8)
            return Encoded::failure(
                "HDR encoding requires six square float16 faces with one mip (1..2048, power of two)");
        for(std::size_t i = 0; i < cubemap.pixels.size(); i += 8) {
            Math::Half4 packed;
            std::memcpy(&packed, cubemap.pixels.data() + i, 8);
            const auto pixel = Math::unpack_half(packed);
            for(int channel = 0; channel < 3; ++channel)
                if(!std::isfinite(pixel[channel]) || pixel[channel] < 0)
                    return Encoded::failure("HDR encoding requires finite nonnegative radiance");
        }
        const int width = cubemap.width * 4;
        const int height = cubemap.height * 2;
        std::string encoded = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " + std::to_string(height)
                              + " +X " + std::to_string(width) + "\n";
        CpuCubeSampler source(cubemap);
        std::vector<std::array<unsigned char, 4>> scanline(width);
        for(int y = 0; y < height; ++y) {
            const float latitude = PI * (y + 0.5f) / height;
            for(int x = 0; x < width; ++x) {
                const float longitude = 2 * PI * ((x + 0.5f) / width - 0.5f);
                const auto radiance =
                    source.sample({std::sin(latitude) * std::cos(longitude), std::cos(latitude),
                                      std::sin(latitude) * std::sin(longitude)},
                        0);
                const float maximum = std::max({radiance.r, radiance.g, radiance.b});
                scanline[x] = {};
                if(maximum < 1e-32f)
                    continue;
                int exponent = 0;
                std::frexp(maximum, &exponent);
                const float scale = std::ldexp(1.0f, 8 - exponent);
                for(int channel = 0; channel < 3; ++channel)
                    scanline[x][channel] = static_cast<unsigned char>(
                        std::clamp(radiance[channel] * scale, 0.0f, 255.0f));
                scanline[x][3] = static_cast<unsigned char>(exponent + 128);
            }
            if(width < 8) {
                encoded.append(reinterpret_cast<const char*>(scanline.data()), scanline.size() * 4);
                continue;
            }
            for(const unsigned char value : {2, 2, width >> 8, width & 255})
                encoded.push_back(static_cast<char>(value));
            for(int channel = 0; channel < 4; ++channel) {
                for(int x = 0; x < width;) {
                    const int count = std::min(width - x, 128);
                    encoded.push_back(static_cast<char>(count));
                    for(int i = 0; i < count; ++i)
                        encoded.push_back(static_cast<char>(scanline[x++][channel]));
                }
            }
        }
        return Encoded::success(std::move(encoded));
    }

    EnvironmentData EnvironmentImporter::prepare_lighting(TextureData background) {
        // BRDF 积分与 HDR 源无关，首次导入时计算一次，后续共用。
        static const auto brdf = integrate_brdf();
        EnvironmentData result;
        result.irradiance = convolve(background, false);
        result.specular = convolve(background, true);
        result.brdf = brdf;
        result.background = std::move(background);
        return result;
    }
}

#include "asset/import/environment_importer.h"

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <numbers>

namespace Comet {
    static constexpr float PI = std::numbers::pi_v<float>;
    static constexpr uint32_t SAMPLE_COUNT = 256;

    static glm::vec3 face_direction(int face, float x, float y) {
        const std::array<glm::vec3, 6> directions{
            {{1, -y, -x}, {-1, -y, x}, {x, 1, y}, {x, -1, -y}, {x, -y, 1}, {-x, -y, -1}}};
        return glm::normalize(directions[face]);
    }

    struct CubeCoordinate {
        int face;
        glm::vec2 uv;
    };

    static CubeCoordinate cube_coordinate(glm::vec3 d) {
        const auto a = glm::abs(d);
        if(a.x >= a.y && a.x >= a.z) {
            if(d.x > 0)
                return {0, {-d.z / a.x, -d.y / a.x}};
            return {1, {d.z / a.x, -d.y / a.x}};
        }
        if(a.y >= a.z) {
            if(d.y > 0)
                return {2, {d.x / a.y, d.z / a.y}};
            return {3, {d.x / a.y, -d.z / a.y}};
        }
        if(d.z > 0)
            return {4, {d.x / a.z, -d.y / a.z}};
        return {5, {-d.x / a.z, -d.y / a.z}};
    }

    class CubeSampler {
    public:
        explicit CubeSampler(const TextureData& data) : m_data(data) {
            size_t offset = 0;
            for(int size = data.width; size > 0; size /= 2) {
                m_offsets.push_back(offset);
                offset += size_t(size) * size * 6 * 8;
            }
        }

        glm::vec3 sample(glm::vec3 direction, float lod) const {
            lod = std::clamp(lod, 0.0f, float(m_data.mip_levels - 1));
            const auto low = static_cast<uint32_t>(lod);
            const auto high = std::min(low + 1, m_data.mip_levels - 1);
            const auto coordinate = cube_coordinate(direction);
            return glm::mix(
                sample_level(coordinate, low), sample_level(coordinate, high), lod - low);
        }

    private:
        glm::vec3 texel(int face, int x, int y, uint32_t mip) const {
            const int size = m_data.width >> mip;
            // 越过立方体面边缘的采样点投影到相邻面，避免边缘钳制造成接缝。
            if(x < 0 || y < 0 || x >= size || y >= size) {
                const auto next = cube_coordinate(face_direction(
                    face, 2.0f * (x + 0.5f) / size - 1, 2.0f * (y + 0.5f) / size - 1));
                face = next.face;
                x = std::clamp(int((next.uv.x * 0.5f + 0.5f) * size), 0, size - 1);
                y = std::clamp(int((next.uv.y * 0.5f + 0.5f) * size), 0, size - 1);
            }
            glm::u16vec4 packed;
            const auto offset = m_offsets[mip] + ((size_t(face) * size + y) * size + x) * 8;
            std::memcpy(&packed, m_data.pixels.data() + offset, sizeof(packed));
            return glm::vec3(glm::unpackHalf(packed));
        }

        glm::vec3 sample_level(CubeCoordinate c, uint32_t mip) const {
            const auto p = (c.uv * 0.5f + 0.5f) * float(m_data.width >> mip) - 0.5f;
            const int x = int(std::floor(p.x)), y = int(std::floor(p.y));
            return glm::mix(
                glm::mix(texel(c.face, x, y, mip), texel(c.face, x + 1, y, mip), p.x - x),
                glm::mix(texel(c.face, x, y + 1, mip), texel(c.face, x + 1, y + 1, mip), p.x - x),
                p.y - y);
        }

        const TextureData& m_data;
        std::vector<size_t> m_offsets;
    };

    static glm::vec2 hammersley(uint32_t index) {
        const float x = float(index) / SAMPLE_COUNT;
        index = (index << 16) | (index >> 16);
        index = ((index & 0x55555555u) << 1) | ((index & 0xAAAAAAAAu) >> 1);
        index = ((index & 0x33333333u) << 2) | ((index & 0xCCCCCCCCu) >> 2);
        index = ((index & 0x0F0F0F0Fu) << 4) | ((index & 0xF0F0F0F0u) >> 4);
        index = ((index & 0x00FF00FFu) << 8) | ((index & 0xFF00FF00u) >> 8);
        return {x, float(index) * 2.3283064365386963e-10f};
    }

    static glm::vec3 hemisphere(float azimuth, float cosine) {
        const float sine = std::sqrt(std::max(0.0f, 1 - cosine * cosine));
        return {sine * std::cos(azimuth), sine * std::sin(azimuth), cosine};
    }

    static glm::vec3 sample_ggx(glm::vec2 xi, float roughness) {
        const float alpha = roughness * roughness;
        const float cosine = std::sqrt((1 - xi.y) / (1 + (alpha * alpha - 1) * xi.y));
        return hemisphere(2 * PI * xi.x, cosine);
    }

    static void append_pixel(TextureData& data, glm::vec3 pixel) {
        const auto packed = glm::packHalf(glm::vec4(glm::clamp(pixel, 0.0f, 65504.0f), 1));
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
                const glm::vec3 v(std::sqrt(1 - nv * nv), 0, nv);
                glm::vec3 sum(0);
                for(uint32_t i = 0; i < SAMPLE_COUNT; ++i) {
                    const auto h = sample_ggx(hammersley(i), roughness);
                    const float vh = std::max(glm::dot(v, h), 0.0f);
                    const auto l = 2 * vh * h - v;
                    if(l.z <= 0)
                        continue;
                    const float visibility = 0.5f
                                             / (nv * std::sqrt(a2 + (1 - a2) * l.z * l.z)
                                                 + l.z * std::sqrt(a2 + (1 - a2) * nv * nv));
                    const float weight = 4 * visibility * l.z * vh / h.z;
                    const float fresnel = std::pow(1 - vh, 5.0f);
                    sum += glm::vec3((1 - fresnel) * weight, fresnel * weight, 0);
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
        const CubeSampler source(background);
        const float texel_angle = 4 * PI / (6 * background.width * background.width);
        for(uint32_t mip = 0; mip < result.mip_levels; ++mip) {
            const int size = result.width >> mip;
            float roughness = 0;
            if(result.mip_levels > 1)
                roughness = float(mip) / float(result.mip_levels - 1);
            struct FilterSample {
                glm::vec3 direction;
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
                    direction = 2 * h.z * h - glm::vec3(0, 0, 1);
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
                        const auto n = face_direction(
                            face, 2.0f * (x + 0.5f) / size - 1, 2.0f * (y + 0.5f) / size - 1);
                        if(specular && mip == 0) {
                            append_pixel(result,
                                source.sample(n, std::log2(float(background.width) / size)));
                            continue;
                        }
                        glm::vec3 up(0, 0, 1);
                        if(std::abs(n.z) > 0.999f)
                            up = {1, 0, 0};
                        const auto tangent = glm::normalize(glm::cross(up, n));
                        const auto bitangent = glm::cross(n, tangent);
                        glm::vec3 sum(0);
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

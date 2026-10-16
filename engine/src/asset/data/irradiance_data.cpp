#include "asset/data/irradiance_data.h"

#include "common/binary.h"
#include "common/file_io.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace Comet {
    namespace {
        constexpr std::string_view MAGIC = "COMETGI1";
        constexpr std::uint32_t VERSION = 1;
        constexpr std::size_t HEADER_SIZE = 56;
        constexpr std::size_t MAX_BYTES = HEADER_SIZE + IrradianceData::MAX_PROBES * 656 + 8;

        std::optional<std::size_t> grid_size(const Math::Vec3u dimensions) {
            std::size_t size = 1;
            for(int axis = 0; axis < 3; ++axis) {
                if(dimensions[axis] < 2 || dimensions[axis] > 64)
                    return std::nullopt;
                size *= dimensions[axis];
            }
            if(size > IrradianceData::MAX_PROBES)
                return std::nullopt;
            return size;
        }

        float nonzero_sign(const float value) {
            return value < 0 ? -1.0f : 1.0f;
        }

        float visibility_weight(
            const IrradianceProbe& probe, const Math::Vec3 delta, const float normal_bias) {
            const float distance = glm::length(delta);
            if(distance < 1e-6f)
                return 1;
            const auto texel = [&](int x, int y) {
                // 八面体边缘折回相邻方向；不能对 UV 做普通 repeat 或 clamp。
                if(x < 0 || x >= 8) {
                    x = x < 0 ? -x - 1 : 15 - x;
                    y = 7 - y;
                }
                if(y < 0 || y >= 8) {
                    y = y < 0 ? -y - 1 : 15 - y;
                    x = 7 - x;
                }
                return probe.visibility[y * 8 + x];
            };
            const auto coordinate = IrradianceData::visibility_coordinate(delta) * 8.0f - .5f;
            const int x = static_cast<int>(std::floor(coordinate.x));
            const int y = static_cast<int>(std::floor(coordinate.y));
            const auto fraction = glm::fract(coordinate);
            const auto moments = glm::mix(glm::mix(texel(x, y), texel(x + 1, y), fraction.x),
                glm::mix(texel(x, y + 1), texel(x + 1, y + 1), fraction.x), fraction.y);
            if(distance <= moments.x + normal_bias)
                return 1;
            const float variance = std::max(moments.y - moments.x * moments.x, 1e-5f);
            const float difference = distance - moments.x;
            const float probability = variance / (variance + difference * difference);
            return probability * probability * probability;
        }
    }

    Result<void> IrradianceData::validate() const {
        const auto count = grid_size(dimensions);
        if(!count || probes.size() != *count)
            return Result<void>::failure(
                "Irradiance grid requires 2..64 cells per axis and at most 4096 probes");
        if(!Math::is_finite(origin) || !Math::is_finite(spacing)
            || glm::any(glm::lessThanEqual(spacing, Math::Vec3(0)))
            || glm::any(glm::greaterThan(spacing, Math::Vec3(10000)))
            || !std::isfinite(max_distance) || max_distance <= 0 || max_distance > 100000)
            return Result<void>::failure("Invalid irradiance grid bounds or distance");
        const auto end = origin + spacing * Math::Vec3(dimensions);
        if(!Math::is_finite(end))
            return Result<void>::failure("Irradiance grid extent is not finite");
        for(const auto& probe : probes) {
            if(probe.coefficients[0].w != 0 && probe.coefficients[0].w != 1)
                return Result<void>::failure("Invalid irradiance probe validity flag");
            for(std::size_t index = 0; index < probe.coefficients.size(); ++index) {
                if(!Math::is_finite(probe.coefficients[index])
                    || (index > 0 && probe.coefficients[index].w != 0))
                    return Result<void>::failure(
                        "Invalid irradiance spherical harmonic coefficient");
            }
            for(const auto moments : probe.visibility) {
                const float squared = moments.x * moments.x;
                if(!Math::is_finite(moments) || moments.x < 0 || moments.x > max_distance
                    || moments.y < squared - std::max(1e-5f, squared * 1e-5f)
                    || moments.y > max_distance * max_distance * 1.00001f)
                    return Result<void>::failure("Invalid irradiance visibility moments");
            }
        }
        return Result<void>::success();
    }

    Math::Vec3 IrradianceData::probe_position(const std::size_t index) const {
        const auto x = index % dimensions.x;
        const auto y = (index / dimensions.x) % dimensions.y;
        const auto z = index / (std::size_t(dimensions.x) * dimensions.y);
        return origin + spacing * Math::Vec3(x, y, z);
    }

    std::array<float, 9> IrradianceData::basis(const Math::Vec3 d) {
        return {0.2820947918f, 0.4886025119f * d.y, 0.4886025119f * d.z, 0.4886025119f * d.x,
            1.0925484306f * d.x * d.y, 1.0925484306f * d.y * d.z,
            0.3153915653f * (3 * d.z * d.z - 1), 1.0925484306f * d.x * d.z,
            0.5462742153f * (d.x * d.x - d.y * d.y)};
    }

    Math::Vec2 IrradianceData::visibility_coordinate(Math::Vec3 direction) {
        direction /= std::abs(direction.x) + std::abs(direction.y) + std::abs(direction.z);
        auto p = Math::Vec2(direction);
        if(direction.z < 0)
            p = Math::Vec2(1 - std::abs(p.y), 1 - std::abs(p.x))
                * Math::Vec2(nonzero_sign(p.x), nonzero_sign(p.y));
        return p * 0.5f + 0.5f;
    }

    Math::Vec3 IrradianceData::visibility_direction(const Math::Vec2 coordinate) {
        const auto p = coordinate * 2.0f - 1.0f;
        Math::Vec3 direction{p.x, p.y, 1 - std::abs(p.x) - std::abs(p.y)};
        if(direction.z < 0) {
            direction.x = (1 - std::abs(p.y)) * nonzero_sign(p.x);
            direction.y = (1 - std::abs(p.x)) * nonzero_sign(p.y);
        }
        return glm::normalize(direction);
    }

    std::optional<Math::Vec3> IrradianceData::sample(
        const Math::Vec3 position, const Math::Vec3 normal, const float normal_bias) const {
        const auto count = grid_size(dimensions);
        const auto direction = Math::normalized_direction(normal);
        if(!count || probes.size() != *count || !direction || !Math::is_finite(position)
            || !Math::is_finite(origin) || !std::isfinite(normal_bias) || normal_bias < 0
            || !Math::is_finite(spacing) || glm::any(glm::lessThanEqual(spacing, Math::Vec3(0))))
            return std::nullopt;
        const auto coordinate = (position - origin) / spacing;
        if(glm::any(glm::lessThan(coordinate, Math::Vec3(-0.5f)))
            || glm::any(glm::greaterThan(coordinate, Math::Vec3(dimensions) - 0.5f)))
            return std::nullopt;
        const auto grid = glm::clamp(coordinate, Math::Vec3(0), Math::Vec3(dimensions) - 1.0f);
        const auto lower = glm::min(Math::Vec3u(grid), dimensions - 2u);
        const auto fraction = grid - Math::Vec3(lower);
        const auto biased = position + *direction * normal_bias;
        const auto harmonics = basis(*direction);
        Math::Vec3 result{0};
        float total = 0;
        bool has_probe = false;
        for(std::uint32_t corner = 0; corner < 8; ++corner) {
            const Math::Vec3u offset{corner & 1u, (corner >> 1u) & 1u, corner >> 2u};
            const auto cell = lower + offset;
            const std::size_t index =
                (std::size_t(cell.z) * dimensions.y + cell.y) * dimensions.x + cell.x;
            const auto& probe = probes[index];
            const auto factors = glm::mix(Math::Vec3(1) - fraction, fraction, Math::Vec3(offset));
            float weight = factors.x * factors.y * factors.z;
            if(!probe.active() || weight == 0)
                continue;
            has_probe = true;
            const auto delta = biased - probe_position(index);
            const float distance = glm::length(delta);
            float facing = 1;
            if(distance > 1e-6f)
                facing = 0.1f + 0.9f * std::max(0.0f, glm::dot(-delta / distance, *direction));
            weight *= facing * facing * visibility_weight(probe, delta, normal_bias);
            Math::Vec3 irradiance{0};
            for(std::size_t coefficient = 0; coefficient < harmonics.size(); ++coefficient)
                irradiance += Math::Vec3(probe.coefficients[coefficient]) * harmonics[coefficient];
            result += glm::max(irradiance, Math::Vec3(0)) * weight;
            total += weight;
        }
        if(!has_probe)
            return std::nullopt;
        // 有探针但被遮挡时返回黑色；不能回退成无视墙壁的全局 IBL。
        if(total < 1e-5f)
            return Math::Vec3(0);
        return result / total;
    }

    Result<std::vector<std::byte>> IrradianceData::encode() const {
        using Encoded = Result<std::vector<std::byte>>;
        if(auto checked = validate(); !checked)
            return Encoded::failure(checked.error());
        Binary::Writer writer(HEADER_SIZE + probes.size() * sizeof(IrradianceProbe) + 8);
        writer.write_bytes(std::as_bytes(std::span(MAGIC)));
        writer.write_u32(VERSION);
        for(int axis = 0; axis < 3; ++axis)
            writer.write_u32(dimensions[axis]);
        for(const auto vector : {origin, spacing})
            for(int axis = 0; axis < 3; ++axis)
                writer.write_float(vector[axis]);
        writer.write_float(max_distance);
        writer.write_u32(static_cast<std::uint32_t>(probes.size()));
        for(const auto& probe : probes) {
            for(const auto coefficient : probe.coefficients)
                for(int axis = 0; axis < 4; ++axis)
                    writer.write_float(coefficient[axis]);
            for(const auto moments : probe.visibility) {
                writer.write_float(moments.x);
                writer.write_float(moments.y);
            }
        }
        writer.write_u64(Binary::hash_bytes(writer.data()));
        return Encoded::success(writer.data());
    }

    Result<IrradianceData> IrradianceData::decode(const std::span<const std::byte> bytes) {
        using Decoded = Result<IrradianceData>;
        if(bytes.size() < HEADER_SIZE + 8 || bytes.size() > MAX_BYTES)
            return Decoded::failure("Invalid irradiance product size");
        Binary::Reader reader(bytes);
        std::uint32_t version = 0, count = 0;
        IrradianceData data;
        if(!reader.match_bytes(std::as_bytes(std::span(MAGIC))) || !reader.read_u32(version)
            || version != VERSION)
            return Decoded::failure("Unsupported irradiance product header");
        for(int axis = 0; axis < 3; ++axis)
            if(!reader.read_u32(data.dimensions[axis]))
                return Decoded::failure("Truncated irradiance grid dimensions");
        for(auto* vector : {&data.origin, &data.spacing})
            for(int axis = 0; axis < 3; ++axis)
                if(!reader.read_float((*vector)[axis]))
                    return Decoded::failure("Truncated irradiance grid bounds");
        if(!reader.read_float(data.max_distance) || !reader.read_u32(count))
            return Decoded::failure("Truncated irradiance product header");
        const auto expected = grid_size(data.dimensions);
        if(!expected || *expected != count || reader.remaining() != std::size_t(count) * 656 + 8)
            return Decoded::failure("Irradiance product grid and payload sizes differ");
        data.probes.resize(count);
        for(auto& probe : data.probes) {
            for(auto& coefficient : probe.coefficients)
                for(int axis = 0; axis < 4; ++axis)
                    if(!reader.read_float(coefficient[axis]))
                        return Decoded::failure("Truncated irradiance coefficients");
            for(auto& moments : probe.visibility)
                if(!reader.read_float(moments.x) || !reader.read_float(moments.y))
                    return Decoded::failure("Truncated irradiance visibility moments");
        }
        std::uint64_t checksum = 0;
        if(!reader.read_u64(checksum)
            || checksum != Binary::hash_bytes(bytes.first(bytes.size() - 8)))
            return Decoded::failure("Irradiance product checksum mismatch");
        if(auto checked = data.validate(); !checked)
            return Decoded::failure(checked.error());
        return Decoded::success(std::move(data));
    }

    Result<void> IrradianceData::save(const std::filesystem::path& path) const {
        auto bytes = encode();
        if(!bytes)
            return Result<void>::failure(bytes.error());
        return write_binary_file_atomic(path, bytes.value());
    }

    Result<IrradianceData> IrradianceData::load(const std::filesystem::path& path) {
        auto bytes = read_binary_file(path, MAX_BYTES);
        if(!bytes)
            return Result<IrradianceData>::failure(bytes.error());
        return decode(bytes.value());
    }
}

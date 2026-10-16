#pragma once

#include "common/export.h"
#include "common/result.h"
#include "core/math_utils.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace Comet {
    // 每探针 41 个 Vec4；系数保存卷积后的 E/pi，visibility 保存距离的一、二阶矩。
    struct alignas(16) IrradianceProbe {
        std::array<Math::Vec4, 9> coefficients{};
        std::array<Math::Vec2, 64> visibility{};

        [[nodiscard]] bool active() const { return coefficients[0].w == 1; }
    };

    struct COMET_API IrradianceData {
        static constexpr std::uint32_t MAX_PROBES = 4096;
        static constexpr std::uint32_t VISIBILITY_SIZE = 8;

        // origin 为首个格心；覆盖区域向外各延伸半个 spacing。
        Math::Vec3 origin{};
        Math::Vec3 spacing{1};
        Math::Vec3u dimensions{2};
        float max_distance = 100;
        std::vector<IrradianceProbe> probes;

        [[nodiscard]] Result<void> validate() const;
        [[nodiscard]] Math::Vec3 probe_position(std::size_t index) const;
        [[nodiscard]] std::optional<Math::Vec3> sample(
            Math::Vec3 position, Math::Vec3 normal, float normal_bias = 0.05f) const;

        [[nodiscard]] Result<std::vector<std::byte>> encode() const;
        [[nodiscard]] static Result<IrradianceData> decode(std::span<const std::byte> bytes);
        [[nodiscard]] Result<void> save(const std::filesystem::path& path) const;
        [[nodiscard]] static Result<IrradianceData> load(const std::filesystem::path& path);

        [[nodiscard]] static std::array<float, 9> basis(Math::Vec3 direction);
        [[nodiscard]] static Math::Vec2 visibility_coordinate(Math::Vec3 direction);
        [[nodiscard]] static Math::Vec3 visibility_direction(Math::Vec2 coordinate);
    };

    static_assert(sizeof(IrradianceProbe) == 656);
    static_assert(offsetof(IrradianceProbe, visibility) == 144);
}

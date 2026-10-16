#pragma once

#include "common/export.h"
#include "common/result.h"
#include "core/math_utils.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Comet {
    // 单个 Detour tile 的只读产品；库版本、引用宽度和端序由查询后端核对。
    struct COMET_API NavigationData {
        static constexpr std::size_t MAX_TILE_BYTES = 64 * 1024 * 1024;
        uint32_t library_version = 0;
        uint32_t reference_bytes = 0;
        uint32_t byte_order = 0;
        uint64_t input_fingerprint = 0;
        Math::Vec3 query_extents{0.5f, 1, 0.5f};
        std::vector<std::byte> tile;

        [[nodiscard]] Result<void> validate() const;
        [[nodiscard]] Result<std::vector<std::byte>> encode() const;
        [[nodiscard]] static Result<NavigationData> decode(std::span<const std::byte> bytes);
        [[nodiscard]] Result<void> save(const std::filesystem::path& path) const;
        [[nodiscard]] static Result<NavigationData> load(const std::filesystem::path& path);
    };
}

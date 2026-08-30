#pragma once

#include "common/export.h"

#include <cstddef>
#include <compare>
#include <cstdint>
#include <functional>

namespace Comet {
    using AssetRevision = std::uint64_t;
    inline constexpr AssetRevision INVALID_ASSET_REVISION = 0;

    class COMET_API AssetHandle {
    public:
        using ValueType = std::uint64_t;

        constexpr AssetHandle() noexcept = default;

        explicit constexpr AssetHandle(const ValueType value) noexcept
            : m_value(value) {}

        [[nodiscard]] static AssetHandle generate();

        [[nodiscard]] constexpr ValueType value() const noexcept {
            return m_value;
        }

        [[nodiscard]] constexpr bool is_valid() const noexcept {
            return m_value != 0;
        }

        [[nodiscard]] explicit constexpr operator bool() const noexcept {
            return is_valid();
        }

        constexpr auto operator<=>(const AssetHandle&) const noexcept = default;

    private:
        ValueType m_value = 0;
    };

    inline constexpr AssetHandle INVALID_ASSET_HANDLE{};
}

template<>
struct std::hash<Comet::AssetHandle> {
    std::size_t operator()(const Comet::AssetHandle handle) const noexcept {
        return std::hash<Comet::AssetHandle::ValueType>{}(handle.value());
    }
};

#pragma once

#include "common/export.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Comet {
    class COMET_API EntityUuid {
    public:
        using Bytes = std::array<std::uint8_t, 16>;

        constexpr EntityUuid() noexcept = default;

        explicit constexpr EntityUuid(const Bytes bytes) noexcept : m_bytes(bytes) {}

        [[nodiscard]] static EntityUuid generate();

        [[nodiscard]] static std::optional<EntityUuid> parse(std::string_view value);

        [[nodiscard]] std::string to_string() const;

        [[nodiscard]] constexpr const Bytes& bytes() const noexcept { return m_bytes; }

        [[nodiscard]] constexpr bool is_valid() const noexcept {
            for(const std::uint8_t byte : m_bytes) {
                if(byte != 0) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] explicit constexpr operator bool() const noexcept {
            return is_valid();
        }

        constexpr auto operator<=>(const EntityUuid&) const noexcept = default;

    private:
        Bytes m_bytes{};
    };

    inline constexpr EntityUuid INVALID_ENTITY_UUID{};
}

template<> struct std::hash<Comet::EntityUuid> {
    std::size_t operator()(const Comet::EntityUuid uuid) const noexcept {
        std::size_t result = 0;
        for(const std::uint8_t byte : uuid.bytes()) {
            result ^= static_cast<std::size_t>(byte)
                      + static_cast<std::size_t>(0x9e3779b97f4a7c15ULL) + (result << 6U)
                      + (result >> 2U);
        }
        return result;
    }
};

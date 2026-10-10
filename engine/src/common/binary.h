#pragma once

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Comet::Binary {
    inline constexpr std::uint64_t HASH_SEED = 14695981039346656037ull;

    [[nodiscard]] inline std::uint64_t hash_bytes(
        std::span<const std::byte> bytes, std::uint64_t hash = HASH_SEED) {
        for(const auto byte : bytes) {
            hash ^= std::to_integer<std::uint8_t>(byte);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    static_assert(sizeof(float) == sizeof(std::uint32_t));
    static_assert(std::numeric_limits<float>::is_iec559);

    class Writer final {
    public:
        explicit Writer(std::size_t capacity = 0) { m_data.reserve(capacity); }

        void write_bytes(std::span<const std::byte> bytes) {
            m_data.insert(m_data.end(), bytes.begin(), bytes.end());
        }

        void write_u32(std::uint32_t value) { write_integer(value); }
        void write_u64(std::uint64_t value) { write_integer(value); }
        void write_float(float value) { write_u32(std::bit_cast<std::uint32_t>(value)); }

        template<std::unsigned_integral Length = std::uint32_t>
        bool write_string(std::string_view value, std::size_t maximum) {
            if(value.size() > maximum || value.size() > std::numeric_limits<Length>::max())
                return false;
            write_integer(static_cast<Length>(value.size()));
            write_bytes(std::as_bytes(std::span(value)));
            return true;
        }

        [[nodiscard]] const std::vector<std::byte>& data() const { return m_data; }

    private:
        template<std::unsigned_integral Integer> void write_integer(Integer value) {
            for(unsigned shift = 0; shift < sizeof(Integer) * 8; shift += 8)
                m_data.push_back(static_cast<std::byte>((value >> shift) & 0xff));
        }

        std::vector<std::byte> m_data;
    };

    class Reader final {
    public:
        explicit Reader(std::span<const std::byte> data) : m_data(data) {}

        [[nodiscard]] bool match_bytes(std::span<const std::byte> expected) {
            if(expected.size() > remaining())
                return false;
            const bool equal =
                std::equal(expected.begin(), expected.end(), m_data.begin() + m_offset);
            m_offset += expected.size();
            return equal;
        }

        [[nodiscard]] bool read_u32(std::uint32_t& value) { return read_integer(value); }
        [[nodiscard]] bool read_u64(std::uint64_t& value) { return read_integer(value); }
        [[nodiscard]] bool read_float(float& value) {
            std::uint32_t bits = 0;
            if(!read_u32(bits))
                return false;
            value = std::bit_cast<float>(bits);
            return true;
        }

        template<std::unsigned_integral Length = std::uint32_t>
        [[nodiscard]] bool read_string(std::string& value, std::size_t maximum) {
            Length size = 0;
            if(!read_integer(size) || size > maximum || size > remaining())
                return false;
            value.assign(reinterpret_cast<const char*>(m_data.data() + m_offset),
                static_cast<std::size_t>(size));
            m_offset += static_cast<std::size_t>(size);
            return true;
        }

        [[nodiscard]] std::size_t remaining() const { return m_data.size() - m_offset; }

    private:
        template<std::unsigned_integral Integer> bool read_integer(Integer& value) {
            if(sizeof(Integer) > remaining())
                return false;
            value = 0;
            for(unsigned shift = 0; shift < sizeof(Integer) * 8; shift += 8)
                value |= static_cast<Integer>(std::to_integer<std::uint8_t>(m_data[m_offset++]))
                         << shift;
            return true;
        }

        std::span<const std::byte> m_data;
        std::size_t m_offset = 0;
    };
}

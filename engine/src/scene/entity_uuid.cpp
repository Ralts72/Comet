#include "scene/entity_uuid.h"

#include <algorithm>
#include <array>
#include <random>

namespace Comet {
    namespace {
        constexpr char HEX_DIGITS[] = "0123456789abcdef";

        int hex_value(const char character) {
            if(character >= '0' && character <= '9') {
                return character - '0';
            }
            if(character >= 'a' && character <= 'f') {
                return character - 'a' + 10;
            }
            if(character >= 'A' && character <= 'F') {
                return character - 'A' + 10;
            }
            return -1;
        }

        std::mt19937_64 make_generator() {
            std::random_device random_device;
            std::array<std::random_device::result_type, 8> seed_data{};
            std::ranges::generate(seed_data, std::ref(random_device));
            std::seed_seq seed(seed_data.begin(), seed_data.end());
            return std::mt19937_64(seed);
        }
    }

    EntityUuid EntityUuid::generate() {
        thread_local std::mt19937_64 generator = make_generator();

        Bytes bytes{};
        for(std::size_t block = 0; block < 2; ++block) {
            const std::uint64_t value = generator();
            for(std::size_t index = 0; index < 8; ++index) {
                bytes[block * 8 + index] = static_cast<std::uint8_t>(
                    value >> (index * 8));
            }
        }

        bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
        bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);
        return EntityUuid(bytes);
    }

    std::optional<EntityUuid> EntityUuid::parse(const std::string_view value) {
        if(value.size() != 36
           || value[8] != '-'
           || value[13] != '-'
           || value[18] != '-'
           || value[23] != '-') {
            return std::nullopt;
        }

        Bytes bytes{};
        std::size_t input_index = 0;
        for(std::size_t byte_index = 0; byte_index < bytes.size(); ++byte_index) {
            if(byte_index == 4 || byte_index == 6
               || byte_index == 8 || byte_index == 10) {
                ++input_index;
            }

            const int high = hex_value(value[input_index]);
            const int low = hex_value(value[input_index + 1]);
            if(high < 0 || low < 0) {
                return std::nullopt;
            }
            bytes[byte_index] = static_cast<std::uint8_t>((high << 4) | low);
            input_index += 2;
        }
        return EntityUuid(bytes);
    }

    std::string EntityUuid::to_string() const {
        std::string result;
        result.reserve(36);
        for(std::size_t index = 0; index < m_bytes.size(); ++index) {
            if(index == 4 || index == 6 || index == 8 || index == 10) {
                result.push_back('-');
            }
            const std::uint8_t byte = m_bytes[index];
            result.push_back(HEX_DIGITS[byte >> 4U]);
            result.push_back(HEX_DIGITS[byte & 0x0fU]);
        }
        return result;
    }
}

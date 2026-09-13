#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace Comet {
    template<typename T> class [[nodiscard]] Result {
    public:
        static Result success(T value) { return Result(std::in_place_index<0>, std::move(value)); }
        static Result failure(std::string error) {
            return Result(std::in_place_index<1>, std::move(error));
        }

        explicit operator bool() const noexcept { return m_state.index() == 0; }
        T& value() & { return std::get<0>(m_state); }
        const T& value() const& { return std::get<0>(m_state); }
        T&& value() && { return std::get<0>(std::move(m_state)); }
        const std::string& error() const& { return std::get<1>(m_state); }

    private:
        template<std::size_t Index, typename Value>
        Result(std::in_place_index_t<Index> index, Value&& value)
            : m_state(index, std::forward<Value>(value)) {}

        std::variant<T, std::string> m_state;
    };

    template<> class [[nodiscard]] Result<void> {
    public:
        static Result success() { return Result(std::nullopt); }
        static Result failure(std::string error) { return Result(std::move(error)); }

        explicit operator bool() const noexcept { return !m_error.has_value(); }
        const std::string& error() const& { return m_error.value(); }

    private:
        explicit Result(std::optional<std::string> error) : m_error(std::move(error)) {}
        std::optional<std::string> m_error;
    };
}

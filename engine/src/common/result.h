#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace Comet {
    template<typename T, typename Error = std::string> class [[nodiscard]] Result {
    public:
        static Result success(T value) { return Result(std::in_place_index<0>, std::move(value)); }
        static Result failure(Error error) {
            return Result(std::in_place_index<1>, std::move(error));
        }

        explicit operator bool() const noexcept { return m_state.index() == 0; }
        T& value() & { return std::get<0>(m_state); }
        const T& value() const& { return std::get<0>(m_state); }
        T&& value() && { return std::get<0>(std::move(m_state)); }
        const Error& error() const& { return std::get<1>(m_state); }

    private:
        template<std::size_t Index, typename Value>
        Result(std::in_place_index_t<Index> index, Value&& value)
            : m_state(index, std::forward<Value>(value)) {}

        std::variant<T, Error> m_state;
    };

    template<typename Error> class [[nodiscard]] Result<void, Error> {
    public:
        static Result success() { return Result(std::nullopt); }
        static Result failure(Error error) { return Result(std::move(error)); }

        explicit operator bool() const noexcept { return !m_error.has_value(); }
        const Error& error() const& { return m_error.value(); }

    private:
        explicit Result(std::optional<Error> error) : m_error(std::move(error)) {}
        std::optional<Error> m_error;
    };
}

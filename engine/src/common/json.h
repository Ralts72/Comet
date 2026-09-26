#pragma once

#include "common/export.h"
#include "common/result.h"
#include <simdjson.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Comet::Json {
    using Node = simdjson::dom::element;

    class COMET_API Context final {
    public:
        Context(std::string_view kind, std::string_view source) : m_kind(kind), m_source(source) {}

        std::string error(std::string_view location, std::string_view detail) const;
        Result<Node> parse(simdjson::dom::parser& parser, std::string_view contents) const;
        Result<simdjson::dom::object> object(Node node, std::string_view location) const;
        Result<simdjson::dom::array> array(Node node, std::string_view location) const;
        Result<Node> required_child(
            Node node, std::string_view key, std::string_view location = "<root>") const;

        template<typename Keys>
        Result<void> validate_keys(
            Node node, const Keys& allowed, std::string_view location = "<root>") const {
            auto fields = object(node, location);
            if(!fields)
                return Result<void>::failure(fields.error());
            for(const auto field : fields.value()) {
                if(std::ranges::find(allowed, field.key) == std::ranges::end(allowed))
                    return Result<void>::failure(
                        error(location, "unknown field '" + std::string(field.key) + "'"));
            }
            return Result<void>::success();
        }

        Result<void> validate_keys(Node node, std::initializer_list<std::string_view> allowed,
            std::string_view location = "<root>") const {
            return validate_keys<decltype(allowed)>(node, allowed, location);
        }

        template<typename T>
        Result<T> read_scalar(
            Node node, std::string_view location, std::string_view expected) const {
            if constexpr(std::is_same_v<T, std::string>) {
                std::string_view value;
                if(!node.get_string().get(value))
                    return Result<T>::success(std::string(value));
            } else if constexpr(std::is_same_v<T, bool>) {
                bool value;
                if(!node.get_bool().get(value))
                    return Result<T>::success(value);
            } else if constexpr(std::is_unsigned_v<T>) {
                std::uint64_t value;
                if(!node.get_uint64().get(value) && value <= std::numeric_limits<T>::max())
                    return Result<T>::success(static_cast<T>(value));
            } else if constexpr(std::is_floating_point_v<T>) {
                double value;
                if(!node.get_double().get(value) && std::isfinite(value)
                    && std::abs(value) <= std::numeric_limits<T>::max())
                    return Result<T>::success(static_cast<T>(value));
            } else {
                static_assert(!sizeof(T), "Unsupported JSON scalar type");
            }
            return Result<T>::failure(error(location, "expected " + std::string(expected)));
        }

        template<typename T>
        Result<T> read_field(Node object, std::string_view key, std::string_view expected,
            std::string_view location = "<root>") const {
            auto child = required_child(object, key, location);
            if(!child)
                return Result<T>::failure(child.error());
            std::string field_location;
            if(location != "<root>") {
                field_location = location;
                field_location += '.';
            }
            field_location += key;
            return read_scalar<T>(child.value(), field_location, expected);
        }

    private:
        std::string_view m_kind;
        std::string_view m_source;
    };

    // simdjson 的 DOM 只读且依赖 parser；写入不持有这些借用节点。
    class COMET_API Writer final {
    public:
        void begin_object();
        void end_object();
        void begin_array();
        void end_array();
        void key(std::string_view name);
        void value(std::string_view value);
        void value(const std::string& value) { this->value(std::string_view(value)); }
        void value(const char* value) { this->value(std::string_view(value)); }
        void value(bool value);
        void value(std::uint64_t value);
        void value(float value);

        template<typename T> void field(std::string_view name, const T& data) {
            key(name);
            value(data);
        }

        Result<std::string> finish() &&;

    private:
        struct Scope {
            bool object;
            bool empty = true;
            bool awaiting_value = false;
        };
        bool before_value();
        void separator();
        void end_scope(bool object);
        void quoted(std::string_view text);

        std::string m_output;
        std::vector<Scope> m_scopes;
        std::string m_error;
    };
}

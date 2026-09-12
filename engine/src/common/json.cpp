#include "common/json.h"

#include <charconv>
#include <unordered_set>
#include <utility>

namespace Comet::Json {
    std::string Context::error(std::string_view location, std::string_view detail) const {
        return "Invalid " + std::string(m_kind) + " '" + std::string(m_source) + "' at '"
               + std::string(location) + "': " + std::string(detail);
    }

    Node Context::parse(simdjson::dom::parser& parser, std::string_view contents) const {
        Node root;
        if(const auto result = parser.parse(contents.data(), contents.size()).get(root))
            throw std::runtime_error(error("<json>", simdjson::error_message(result)));
        return root;
    }

    simdjson::dom::object Context::object(Node node, std::string_view location) const {
        simdjson::dom::object result;
        if(node.get_object().get(result))
            throw std::runtime_error(error(location, "expected an object"));
        std::unordered_set<std::string_view> keys;
        for(const auto field : result) {
            if(!keys.insert(field.key).second)
                throw std::runtime_error(
                    error(location, "duplicate field '" + std::string(field.key) + "'"));
        }
        return result;
    }

    simdjson::dom::array Context::array(Node node, std::string_view location) const {
        simdjson::dom::array result;
        if(node.get_array().get(result))
            throw std::runtime_error(error(location, "expected an array"));
        return result;
    }

    Node Context::required_child(
        Node node, std::string_view key, std::string_view location) const {
        Node child;
        if(node[key].get(child))
            throw std::runtime_error(
                error(location, "missing required field '" + std::string(key) + "'"));
        return child;
    }

    void Writer::separator() {
        auto& scope = m_scopes.back();
        if(!scope.empty)
            m_output += ',';
        m_output += '\n';
        m_output.append(m_scopes.size() * 2, ' ');
        scope.empty = false;
    }

    void Writer::before_value() {
        if(m_scopes.empty()) {
            if(!m_output.empty())
                throw std::runtime_error("JSON writer already has a root value");
        } else if(m_scopes.back().object) {
            if(!m_scopes.back().awaiting_value)
                throw std::runtime_error("JSON object value requires a key");
            m_scopes.back().awaiting_value = false;
        } else {
            separator();
        }
    }

    void Writer::begin_object() {
        before_value();
        m_output += '{';
        m_scopes.push_back({.object = true});
    }

    void Writer::begin_array() {
        before_value();
        m_output += '[';
        m_scopes.push_back({.object = false});
    }

    void Writer::end_scope(bool object) {
        if(m_scopes.empty() || m_scopes.back().object != object
            || m_scopes.back().awaiting_value)
            throw std::runtime_error("Unbalanced JSON writer scope");
        const bool empty = m_scopes.back().empty;
        m_scopes.pop_back();
        if(!empty) {
            m_output += '\n';
            m_output.append(m_scopes.size() * 2, ' ');
        }
        m_output += object ? '}' : ']';
    }

    void Writer::end_object() {
        end_scope(true);
    }
    void Writer::end_array() {
        end_scope(false);
    }

    void Writer::quoted(std::string_view text) {
        if(!simdjson::validate_utf8(text))
            throw std::runtime_error("Cannot serialize invalid UTF-8 as JSON");
        constexpr char hex[] = "0123456789abcdef";
        m_output += '"';
        for(const unsigned char c : text) {
            if(c == '"' || c == '\\') {
                m_output += '\\';
                m_output += static_cast<char>(c);
            } else if(c < 0x20) {
                m_output += "\\u00";
                m_output += hex[c >> 4];
                m_output += hex[c & 15];
            } else {
                m_output += static_cast<char>(c);
            }
        }
        m_output += '"';
    }

    void Writer::key(std::string_view name) {
        if(m_scopes.empty() || !m_scopes.back().object || m_scopes.back().awaiting_value)
            throw std::runtime_error("JSON key requires an object and a completed value");
        separator();
        quoted(name);
        m_output += ": ";
        m_scopes.back().awaiting_value = true;
    }

    void Writer::value(std::string_view data) {
        before_value();
        quoted(data);
    }

    void Writer::value(bool data) {
        before_value();
        m_output += data ? "true" : "false";
    }

    void Writer::value(std::uint64_t data) {
        before_value();
        m_output += std::to_string(data);
    }

    void Writer::value(float data) {
        if(!std::isfinite(data))
            throw std::runtime_error("Cannot serialize a non-finite JSON number");
        char buffer[64];
        const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), data);
        if(error != std::errc{})
            throw std::runtime_error("Cannot serialize JSON number");
        before_value();
        m_output.append(buffer, end);
    }

    std::string Writer::finish() && {
        if(!m_scopes.empty() || m_output.empty())
            throw std::runtime_error("Incomplete JSON document");
        m_output += '\n';
        return std::move(m_output);
    }
}

#include "common/json.h"

#include <charconv>
#include <unordered_set>
#include <utility>

namespace Comet::Json {
    std::string Context::error(std::string_view location, std::string_view detail) const {
        return "Invalid " + std::string(m_kind) + " '" + std::string(m_source) + "' at '"
               + std::string(location) + "': " + std::string(detail);
    }

    Result<Node> Context::parse(simdjson::dom::parser& parser, std::string_view contents) const {
        Node root;
        if(const auto result = parser.parse(contents.data(), contents.size()).get(root))
            return Result<Node>::failure(error("<json>", simdjson::error_message(result)));
        return Result<Node>::success(root);
    }

    Result<simdjson::dom::object> Context::object(Node node, std::string_view location) const {
        simdjson::dom::object result;
        if(node.get_object().get(result))
            return Result<simdjson::dom::object>::failure(error(location, "expected an object"));
        std::unordered_set<std::string_view> keys;
        for(const auto field : result) {
            if(!keys.insert(field.key).second)
                return Result<simdjson::dom::object>::failure(
                    error(location, "duplicate field '" + std::string(field.key) + "'"));
        }
        return Result<simdjson::dom::object>::success(result);
    }

    Result<simdjson::dom::array> Context::array(Node node, std::string_view location) const {
        simdjson::dom::array result;
        if(node.get_array().get(result))
            return Result<simdjson::dom::array>::failure(error(location, "expected an array"));
        return Result<simdjson::dom::array>::success(result);
    }

    Result<Node> Context::required_child(
        Node node, std::string_view key, std::string_view location) const {
        Node child;
        if(node[key].get(child))
            return Result<Node>::failure(
                error(location, "missing required field '" + std::string(key) + "'"));
        return Result<Node>::success(child);
    }

    void Writer::separator() {
        auto& scope = m_scopes.back();
        if(!scope.empty)
            m_output += ',';
        m_output += '\n';
        m_output.append(m_scopes.size() * 2, ' ');
        scope.empty = false;
    }

    bool Writer::before_value() {
        if(!m_error.empty())
            return false;
        if(m_scopes.empty()) {
            if(!m_output.empty()) {
                m_error = "JSON writer already has a root value";
                return false;
            }
        } else if(m_scopes.back().object) {
            if(!m_scopes.back().awaiting_value) {
                m_error = "JSON object value requires a key";
                return false;
            }
            m_scopes.back().awaiting_value = false;
        } else {
            separator();
        }
        return true;
    }

    void Writer::begin_object() {
        if(!before_value())
            return;
        m_output += '{';
        m_scopes.push_back({.object = true});
    }

    void Writer::begin_array() {
        if(!before_value())
            return;
        m_output += '[';
        m_scopes.push_back({.object = false});
    }

    void Writer::end_scope(bool object) {
        if(!m_error.empty())
            return;
        if(m_scopes.empty() || m_scopes.back().object != object || m_scopes.back().awaiting_value) {
            m_error = "Unbalanced JSON writer scope";
            return;
        }
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
        if(!simdjson::validate_utf8(text)) {
            m_error = "Cannot serialize invalid UTF-8 as JSON";
            return;
        }
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
        if(!m_error.empty())
            return;
        if(m_scopes.empty() || !m_scopes.back().object || m_scopes.back().awaiting_value) {
            m_error = "JSON key requires an object and a completed value";
            return;
        }
        separator();
        quoted(name);
        m_output += ": ";
        m_scopes.back().awaiting_value = true;
    }

    void Writer::value(std::string_view data) {
        if(!before_value())
            return;
        quoted(data);
    }

    void Writer::value(bool data) {
        if(!before_value())
            return;
        m_output += data ? "true" : "false";
    }

    void Writer::value(std::uint64_t data) {
        if(!before_value())
            return;
        m_output += std::to_string(data);
    }

    void Writer::value(float data) {
        if(!m_error.empty())
            return;
        if(!std::isfinite(data)) {
            m_error = "Cannot serialize a non-finite JSON number";
            return;
        }
        char buffer[64];
        const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), data);
        if(error != std::errc{}) {
            m_error = "Cannot serialize JSON number";
            return;
        }
        if(!before_value())
            return;
        m_output.append(buffer, end);
    }

    Result<std::string> Writer::finish() && {
        if(!m_error.empty())
            return Result<std::string>::failure(std::move(m_error));
        if(!m_scopes.empty() || m_output.empty())
            return Result<std::string>::failure("Incomplete JSON document");
        m_output += '\n';
        return Result<std::string>::success(std::move(m_output));
    }
}

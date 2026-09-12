#include "asset/serialization/yaml_serialization.h"

namespace Comet::AssetSerialization {
    std::string YamlContext::error(
        const std::string_view location, const std::string_view detail) const {
        return "Invalid " + std::string(m_kind) + " '" + std::string(m_source) + "' at '"
               + std::string(location) + "': " + std::string(detail);
    }

    void YamlContext::require_map(
        const YAML::Node& node, const std::string_view location) const {
        Yaml::require_map(node, m_source, location, error_factory());
    }

    void YamlContext::validate_keys(const YAML::Node& node,
        const std::initializer_list<std::string_view> allowed,
        const std::string_view location) const {
        Yaml::validate_keys(node, allowed, m_source, location, error_factory());
    }

    YAML::Node YamlContext::required_child(const YAML::Node& node,
        const std::string_view key, const std::string_view location) const {
        return Yaml::required_child(node, key, m_source, location, error_factory());
    }
}

#pragma once

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Comet::Yaml {
    using ErrorFactory = std::runtime_error (*)(
        std::string_view source, std::string_view location, const std::string& detail);

    inline void require_map(const YAML::Node& node, const std::string_view source,
        const std::string_view location, const ErrorFactory error) {
        if(!node.IsDefined() || !node.IsMap()) {
            throw error(source, location, "expected a mapping");
        }
    }

    inline void require_sequence(const YAML::Node& node, const std::string_view source,
        const std::string_view location, const ErrorFactory error) {
        if(!node.IsDefined() || !node.IsSequence()) {
            throw error(source, location, "expected a sequence");
        }
    }

    template<typename AllowedKeys>
    void validate_keys(const YAML::Node& node, const AllowedKeys& allowed,
        const std::string_view source, const std::string_view location,
        const ErrorFactory error) {
        require_map(node, source, location, error);
        std::unordered_set<std::string> found;
        for(const auto& entry : node) {
            std::string key;
            try {
                key = entry.first.as<std::string>();
            } catch(const YAML::Exception&) {
                throw error(source, location, "expected string keys");
            }

            if(!found.insert(key).second) {
                throw error(source, location, "duplicate field '" + key + "'");
            }
            if(std::ranges::find(allowed, key) == std::ranges::end(allowed)) {
                throw error(source, location, "unknown field '" + key + "'");
            }
        }
    }

    inline YAML::Node required_child(const YAML::Node& node, const std::string_view key,
        const std::string_view source, const std::string_view location,
        const ErrorFactory error) {
        const YAML::Node child = node[std::string(key)];
        if(!child.IsDefined()) {
            throw error(
                source, location, "missing required field '" + std::string(key) + "'");
        }
        return child;
    }

    template<typename T>
    T read_scalar(const YAML::Node& node, const std::string_view source,
        const std::string_view location, const std::string_view expected,
        const ErrorFactory error) {
        if(!node.IsDefined() || !node.IsScalar()) {
            throw error(source, location, "expected " + std::string(expected));
        }

        try {
            return node.as<T>();
        } catch(const YAML::Exception&) {
            throw error(source, location, "expected " + std::string(expected));
        }
    }
}

#pragma once

#include "common/serialization.h"
#include <yaml-cpp/yaml.h>

namespace Comet::Yaml {
    class COMET_API Context final: public Serialization::Context {
    public:
        using Serialization::Context::Context;

        Result<YAML::Node> parse(std::string_view contents) const;
        Result<void> mapping(const YAML::Node& node, std::string_view location) const;
        // 缺失的可选路径返回 Undefined；存在但类型错误的父节点返回失败。
        Result<YAML::Node> find(const YAML::Node& root, std::string_view path) const;

        template<typename T>
        Result<T> read_scalar(
            const YAML::Node& node, std::string_view location, std::string_view expected) const {
            T value{};
            if(!node.IsScalar() || !YAML::convert<T>::decode(node, value))
                return Result<T>::failure(error(location, "expected " + std::string(expected)));
            return Result<T>::success(std::move(value));
        }
    };

    [[nodiscard]] COMET_API bool is_string(const YAML::Node& node);
}

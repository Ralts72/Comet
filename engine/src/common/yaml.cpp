#include "common/yaml.h"

#include <unordered_set>

namespace Comet::Yaml {
    Result<YAML::Node> Context::parse(std::string_view contents) const {
        try {
            return Result<YAML::Node>::success(YAML::Load(std::string(contents)));
        } catch(const YAML::Exception& exception) {
            return Result<YAML::Node>::failure(error("<yaml>", exception.what()));
        }
    }

    Result<void> Context::mapping(const YAML::Node& node, std::string_view location) const {
        if(!node.IsMap())
            return Result<void>::failure(error(location, "expected a mapping"));
        std::unordered_set<std::string_view> keys;
        for(const auto& entry : node) {
            if(!entry.first.IsScalar())
                return Result<void>::failure(error(location, "expected a string key"));
            const auto& key = entry.first.Scalar();
            if(!keys.insert(key).second)
                return Result<void>::failure(error(location, "duplicate field '" + key + "'"));
        }
        return Result<void>::success();
    }

    Result<YAML::Node> Context::find(const YAML::Node& root, std::string_view path) const {
        using Found = Result<YAML::Node>;
        YAML::Node node = root;
        if(!node.IsDefined() || node.IsNull())
            return Found::success(YAML::Node(YAML::NodeType::Undefined));
        std::string location;
        while(!path.empty()) {
            if(auto valid = mapping(node, location.empty() ? "<root>" : location); !valid)
                return Found::failure(valid.error());
            const auto separator = path.find('.');
            const auto segment = path.substr(0, separator);
            const auto child = static_cast<const YAML::Node&>(node)[std::string(segment)];
            if(!child.IsDefined())
                return Found::success(YAML::Node(YAML::NodeType::Undefined));
            node.reset(child);
            if(!location.empty())
                location += '.';
            location += segment;
            if(separator == std::string_view::npos)
                break;
            path.remove_prefix(separator + 1);
        }
        return Found::success(node);
    }

    bool is_string(const YAML::Node& node) {
        if(!node.IsScalar())
            return false;
        if(node.Tag() == "!" || node.Tag() == "tag:yaml.org,2002:str")
            return true;
        if(node.Tag() != "?")
            return false;
        bool boolean = false;
        double number = 0;
        return !YAML::convert<bool>::decode(node, boolean)
               && !YAML::convert<double>::decode(node, number);
    }
}

#include "asset/script.h"
#include "asset/data/script_sources.h"

#include <algorithm>

namespace Comet {
    using namespace ScriptSource;

    namespace {
        constexpr std::size_t MAX_MODULE_NAME = 256;
        bool valid_module_name(const std::string_view name) {
            if(name.empty() || name.size() > MAX_MODULE_NAME)
                return false;
            bool first = true;
            for(const unsigned char value : name) {
                if(value == '.') {
                    if(first)
                        return false;
                    first = true;
                    continue;
                }
                const bool letter = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
                                    || value == '_';
                if(!letter && (first || value < '0' || value > '9'))
                    return false;
                first = false;
            }
            return !first;
        }
    }

    Result<std::filesystem::path> Script::module_path(const std::string_view name) {
        if(!valid_module_name(name))
            return Result<std::filesystem::path>::failure(
                "require expects a bounded dotted module name");
        std::string relative(name);
        std::ranges::replace(relative, '.', '/');
        return Result<std::filesystem::path>::success(relative + ".module.lua");
    }

    Result<std::string> Script::module_name(const std::filesystem::path& relative_path) {
        constexpr std::string_view suffix = ".module.lua";
        const auto path = relative_path.generic_string();
        if(!safe_source_path(relative_path) || !path.ends_with(suffix))
            return Result<std::string>::failure(
                "Module source must be a project-relative .module.lua file");
        auto name = path.substr(0, path.size() - suffix.size());
        std::ranges::replace(name, '/', '.');
        const auto resolved = module_path(name);
        if(!resolved || resolved.value() != relative_path)
            return Result<std::string>::failure(
                "Module path must use ASCII identifier segments with at most 256 name bytes");
        return Result<std::string>::success(std::move(name));
    }

    bool Script::inputs_are_current() const {
        return !m_sources || m_sources->inputs_are_current();
    }

    bool Script::has_same_sources(const Script& other) const {
        if(m_name != other.m_name || m_source_path != other.m_source_path
            || m_dependencies != other.m_dependencies
            || static_cast<bool>(m_sources) != static_cast<bool>(other.m_sources))
            return false;
        if(!m_sources)
            return m_source == other.m_source;
        if(m_sources->files.at(m_source_path).source
            != other.m_sources->files.at(other.m_source_path).source)
            return false;
        for(const auto& path : m_dependencies)
            if(m_sources->files.at(path).source != other.m_sources->files.at(path).source)
                return false;
        return true;
    }

    bool Script::LoadFailure::inputs_are_current() const {
        return !m_sources || m_sources->inputs_are_current();
    }

    Result<void, Error> Script::validate_overrides(const ParameterMap& overrides) const {
        if(!valid_parameters(overrides))
            return Result<void, Error>::failure({"Invalid script parameter values"});
        for(const auto& [name, value] : overrides) {
            const auto found = m_properties.find(name);
            if(found == m_properties.end() || found->second.default_value.index() != value.index())
                return Result<void, Error>::failure(
                    {"Script parameter no longer matches declaration: " + name});
        }
        return Result<void, Error>::success();
    }

    void Script::retain_compatible_overrides(ParameterMap& overrides) const {
        std::erase_if(overrides, [&](const auto& value) {
            const auto property = m_properties.find(value.first);
            return property == m_properties.end()
                   || property->second.default_value.index() != value.second.index();
        });
    }

    Result<ParameterMap, Error> Script::resolve_parameters(const ParameterMap& overrides) const {
        if(auto checked = validate_overrides(overrides); !checked)
            return Result<ParameterMap, Error>::failure(checked.error());
        ParameterMap values;
        for(const auto& [name, property] : m_properties)
            values.emplace(name, property.default_value);
        for(const auto& [name, value] : overrides)
            values.at(name) = value;
        return Result<ParameterMap, Error>::success(std::move(values));
    }
}

#pragma once

#include "common/parameters.h"
#include "common/error.h"
#include "common/result.h"

#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Comet {
    struct ScriptSources;
    class ScriptInstance;
    // 不可变源码与字段定义；运行实例不存入资产缓存。
    class COMET_API Script final {
    public:
        struct Property {
            enum class Semantic { Default, Color };
            ParameterValue default_value;
            Semantic semantic = Semantic::Default;
        };
        using PropertyMap = std::map<std::string, Property>;
        using EventHandlers = std::map<std::string, std::string>;
        struct COMET_API LoadFailure {
            std::string message;
            std::map<std::filesystem::path, std::vector<std::filesystem::path>> dependencies;

            LoadFailure() = default;
            LoadFailure(std::string text,
                std::map<std::filesystem::path, std::vector<std::filesystem::path>> paths = {})
                : message(std::move(text)), dependencies(std::move(paths)) {}
            [[nodiscard]] bool inputs_are_current() const;

        private:
            friend class Script;
            std::shared_ptr<const ScriptSources> m_sources;
        };

        [[nodiscard]] static Result<std::shared_ptr<Script>, Error> load(
            const std::filesystem::path& path);
        [[nodiscard]] static Result<std::shared_ptr<Script>, Error> create(
            std::string source, std::string name = "<script>");
        // 整批共享只读源码；失败仍返回各入口已尝试的项目内模块路径。
        [[nodiscard]] static Result<std::vector<std::shared_ptr<Script>>, LoadFailure> load_group(
            const std::filesystem::path& assets_root,
            std::span<const std::filesystem::path> relative_paths);
        [[nodiscard]] static Result<std::filesystem::path> module_path(std::string_view name);
        [[nodiscard]] static Result<std::string> module_name(
            const std::filesystem::path& relative_path);
        [[nodiscard]] Result<void, Error> validate_overrides(const ParameterMap& overrides) const;
        // 只移除缺失声明或存储类型不匹配的覆盖；值合法性仍由严格校验负责。
        void retain_compatible_overrides(ParameterMap& overrides) const;
        [[nodiscard]] Result<ParameterMap, Error> resolve_parameters(
            const ParameterMap& overrides) const;
        [[nodiscard]] const PropertyMap& properties() const { return m_properties; }
        [[nodiscard]] const EventHandlers& event_handlers() const { return m_event_handlers; }
        [[nodiscard]] const std::filesystem::path& source_path() const { return m_source_path; }
        [[nodiscard]] const std::vector<std::filesystem::path>& dependencies() const {
            return m_dependencies;
        }
        [[nodiscard]] bool inputs_are_current() const;
        [[nodiscard]] bool has_same_sources(const Script& other) const;

    private:
        friend class ScriptInstance;
        friend Result<void, Error> prepare_script_definition(
            Script&, ScriptSources*, std::vector<std::filesystem::path>*);

        std::string m_source;
        std::string m_name;
        std::filesystem::path m_source_path;
        std::vector<std::filesystem::path> m_dependencies;
        std::shared_ptr<const ScriptSources> m_sources;
        PropertyMap m_properties;
        EventHandlers m_event_handlers;
    };
}

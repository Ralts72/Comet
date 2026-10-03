#pragma once

#include "scene/entity.h"
#include "scene/property.h"
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
    class Entity;
    class InputState;
    class MaterialParameterValidator;
    class Scene;
    // 不可变源码与字段定义；运行实例不存入资产缓存。
    class COMET_API Script final {
        struct SourceSet;

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
            std::shared_ptr<const SourceSet> m_sources;
        };

        enum class Phase {
            Start,
            FixedUpdate,
            Update,
            Stop,
            CollisionEnter,
            CollisionExit,
            TriggerEnter,
            TriggerExit,
            Event
        };
        struct Invocation {
            double delta_time = 0;
            Scene* scene = nullptr;
            const InputState* input = nullptr;
            Entity contact_other;
            const MaterialParameterValidator* materials = nullptr;
            std::string_view event_handler;
            const ParameterValue* event_value = nullptr;
        };
        class COMET_API Instance final {
        public:
            ~Instance();
            Instance(const Instance&) = delete;
            Instance& operator=(const Instance&) = delete;
            Result<void, Error> invoke(
                Phase phase, Entity entity, const ParameterMap& parameters, Invocation invocation);
            Result<void, Error> invoke(Phase phase, Entity entity, const ParameterMap& parameters);

        private:
            friend class Script;
            struct Impl;
            explicit Instance(std::unique_ptr<Impl> impl);
            std::unique_ptr<Impl> m_impl;
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
        [[nodiscard]] Result<std::unique_ptr<Instance>, Error> instantiate() const;
        [[nodiscard]] Result<void, Error> validate_overrides(const ParameterMap& overrides) const;
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
        [[nodiscard]] Result<std::unique_ptr<Instance>, Error> instantiate(
            SourceSet* collecting, std::vector<std::filesystem::path>* dependencies) const;
        Result<void, Error> prepare_definition(SourceSet* collecting = nullptr,
            std::vector<std::filesystem::path>* dependencies = nullptr);

        std::string m_source;
        std::string m_name;
        std::filesystem::path m_source_path;
        std::vector<std::filesystem::path> m_dependencies;
        std::shared_ptr<const SourceSet> m_sources;
        PropertyMap m_properties;
        EventHandlers m_event_handlers;
    };
}

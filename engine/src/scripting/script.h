#pragma once

#include "scene/entity.h"
#include "scene/property.h"
#include "common/error.h"
#include "common/result.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace Comet {
    class Entity;
    class InputState;
    class MaterialParameterValidator;
    class Scene;
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
        [[nodiscard]] Result<std::unique_ptr<Instance>, Error> instantiate() const;
        [[nodiscard]] Result<void, Error> validate_overrides(const ParameterMap& overrides) const;
        [[nodiscard]] Result<ParameterMap, Error> resolve_parameters(
            const ParameterMap& overrides) const;
        [[nodiscard]] const PropertyMap& properties() const { return m_properties; }
        [[nodiscard]] const EventHandlers& event_handlers() const { return m_event_handlers; }

    private:
        std::string m_source;
        std::string m_name;
        PropertyMap m_properties;
        EventHandlers m_event_handlers;
    };
}

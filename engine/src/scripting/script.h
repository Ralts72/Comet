#pragma once

#include "scene/entity.h"
#include "scene/property.h"
#include "common/error.h"
#include "common/result.h"

#include <filesystem>
#include <memory>

namespace Comet {
    class Entity;
    class InputState;
    class Scene;
    // 不可变源码与字段默认值；运行实例不存入资产缓存。
    class COMET_API Script final {
    public:
        enum class Phase {
            Start, FixedUpdate, Update, Stop,
            CollisionEnter, CollisionExit, TriggerEnter, TriggerExit
        };
        struct Invocation {
            double delta_time = 0;
            Scene* scene = nullptr;
            const InputState* input = nullptr;
            Entity contact_other;
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
        [[nodiscard]] const ParameterMap& defaults() const { return m_defaults; }

    private:
        std::string m_source;
        std::string m_name;
        ParameterMap m_defaults;
    };
}

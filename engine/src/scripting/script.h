#pragma once

#include "scene/property.h"
#include "common/error.h"
#include "common/result.h"
#include "core/input.h"

#include <filesystem>
#include <memory>

namespace Comet {
    class Entity;
    // 不可变源码与字段默认值；运行实例不存入资产缓存。
    class COMET_API Script final {
    public:
        enum class Phase { Start, FixedUpdate, Update, Stop };
        struct Invocation {
            double delta_time = 0;
            const Input::Frame* input = nullptr;
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
        [[nodiscard]] Result<ParameterMap, Error> parameters(const ParameterMap& overrides) const;
        [[nodiscard]] const ParameterMap& defaults() const { return m_defaults; }

    private:
        std::string m_source;
        std::string m_name;
        ParameterMap m_defaults;
    };
}

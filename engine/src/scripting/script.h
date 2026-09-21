#pragma once

#include "scene/property.h"
#include "scene/entity.h"
#include "scene/systems/system.h"

#include <filesystem>
#include <memory>

namespace Comet {
    // 不可变源码与字段默认值；运行实例不存入资产缓存。
    class COMET_API Script final {
    public:
        enum class Phase { Start, FixedUpdate, Update, Stop };
        class COMET_API Instance final {
        public:
            ~Instance();
            Instance(const Instance&) = delete;
            Instance& operator=(const Instance&) = delete;
            Result<void, Error> invoke(Phase phase, Entity entity, const ParameterMap& parameters,
                const System::Context* context = nullptr);

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

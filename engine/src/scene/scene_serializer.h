#pragma once

#include "common/export.h"
#include "common/result.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace Comet {
    class ComponentRegistry;
    class Scene;

    class COMET_API SceneSerializer final {
    public:
        static constexpr std::uint32_t FORMAT_VERSION = 2;
        static constexpr std::size_t MAX_HIERARCHY_DEPTH = 128;

        explicit SceneSerializer(const ComponentRegistry& component_registry);

        [[nodiscard]] Result<std::string> serialize(const Scene& scene) const;

        [[nodiscard]] Result<std::unique_ptr<Scene>> deserialize(
            std::string_view contents, std::string_view source = "<memory>") const;

        [[nodiscard]] Result<std::unique_ptr<Scene>> clone(const Scene& scene) const;

        [[nodiscard]] Result<void> save(const Scene& scene, const std::string& path) const;

        [[nodiscard]] Result<std::unique_ptr<Scene>> load(const std::string& path) const;

    private:
        const ComponentRegistry& m_component_registry;
    };
}

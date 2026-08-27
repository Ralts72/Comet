#pragma once

#include "common/export.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace Comet {
    class ComponentRegistry;
    class Scene;

    class COMET_API SceneSerializer final {
    public:
        static constexpr std::uint32_t FORMAT_VERSION = 1;

        explicit SceneSerializer(
            const ComponentRegistry& component_registry);

        [[nodiscard]] std::string serialize(const Scene& scene) const;

        [[nodiscard]] std::unique_ptr<Scene> deserialize(
            std::string_view contents,
            std::string_view source = "<memory>") const;

        [[nodiscard]] std::unique_ptr<Scene> clone(
            const Scene& scene) const;

        void save(const Scene& scene, const std::string& path) const;

        [[nodiscard]] std::unique_ptr<Scene> load(
            const std::string& path) const;

    private:
        const ComponentRegistry& m_component_registry;
    };
}

#pragma once

#include "scene/entity_uuid.h"

#include <cstdint>
#include <optional>

namespace Comet {
    class Scene;
}

namespace CometEditor {
    struct EntityDragPayload {
        static constexpr const char* TYPE = "COMET_ENTITY_UUID";
        Comet::EntityUuid entity;
        std::uint64_t generation;
    };

    [[nodiscard]] std::optional<Comet::EntityUuid> accept_entity_drop(
        Comet::Scene& scene, std::uint64_t generation);
    [[nodiscard]] bool edit_entity_reference(const char* label, Comet::EntityUuid& reference,
        Comet::Scene& scene, std::optional<std::uint64_t> drop_generation = std::nullopt);
}

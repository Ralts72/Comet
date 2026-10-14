#pragma once
#include "animation/animation_controller.h"
#include <filesystem>

namespace Comet {
    class COMET_API AnimationControllerSerializer {
    public:
        [[nodiscard]] Result<AnimationController> deserialize(
            std::string_view contents, std::string_view source = "<memory>") const;
        [[nodiscard]] Result<std::string> serialize(const AnimationController& controller) const;
        [[nodiscard]] Result<AnimationController> load(const std::filesystem::path& path) const;
        [[nodiscard]] Result<void> save(
            const AnimationController& controller, const std::filesystem::path& path) const;
    };
}

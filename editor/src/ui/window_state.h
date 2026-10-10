#pragma once

#include "common/result.h"
#include "core/window_settings.h"

#include <filesystem>
#include <optional>

namespace Comet {
    class Window;
}

namespace CometEditor {
    struct WindowState {
        int width = 0;
        int height = 0;
        bool maximized = false;

        [[nodiscard]] static Comet::Result<std::optional<WindowState>> load(
            const std::filesystem::path& path);
        [[nodiscard]] static WindowState capture(const Comet::Window& window);
        [[nodiscard]] Comet::Result<void> save(const std::filesystem::path& path) const;
        void apply_to(Comet::WindowSettings& settings) const;
    };
}

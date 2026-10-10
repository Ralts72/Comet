#pragma once

#include "common/json.h"
#include "core/window_settings.h"

namespace Comet {
    // 游戏窗口的逻辑尺寸；全屏使用显示器模式，内部渲染分辨率独立。
    struct COMET_API DisplaySettings {
        int width = 960;
        int height = 720;
        WindowMode mode = WindowMode::Windowed;
        bool vsync = false;

        [[nodiscard]] Result<void> validate() const;
        [[nodiscard]] static Result<DisplaySettings> read(
            Json::Node node, const Json::Context& context, std::string_view location);
        void write(Json::Writer& writer) const;
        [[nodiscard]] static Result<WindowMode> parse_mode(std::string_view name);
        [[nodiscard]] static std::string_view mode_name(WindowMode mode);
        bool operator==(const DisplaySettings&) const = default;
    };
}

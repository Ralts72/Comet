#pragma once

#include "core/math_utils.h"

#include <optional>

namespace Comet::Ui {
    enum class RenderOutput { Presentation, Offscreen };

    // 窗口逻辑坐标中的显示区域，与 UI 的实际像素尺寸独立。
    struct View {
        struct Clip {
            Math::Vec2 origin{};
            Math::Vec2 size{};
        };
        Math::Vec2 origin{};
        Math::Vec2 size{};
        Math::Vec2u pixel_size{};
        float density = 1;
        std::optional<Clip> clip;

        [[nodiscard]] bool contains(Math::Vec2 point) const {
            const auto inside = [point](Math::Vec2 start, Math::Vec2 extent) {
                return point.x >= start.x && point.y >= start.y && point.x < start.x + extent.x
                       && point.y < start.y + extent.y;
            };
            return inside(origin, size) && (!clip || inside(clip->origin, clip->size));
        }
    };
}

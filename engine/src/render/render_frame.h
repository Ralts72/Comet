#pragma once

#include "render/overlay_record_context.h"
#include "render/debug/line_draw_list.h"
#include "render/scene/render_scene.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Comet {
    struct RenderWorld;

    // Main 一次性交付世界、视图请求和 UI；Worker 不读取下一帧的输入。
    struct RenderFrame {
        struct Pick {
            Math::Vec2u pixel;
            Math::Vec2u image_resolution;
            uint64_t scene_lifetime;
        };
        struct View {
            uint32_t id;
            RenderView parameters;
            bool draw;
            std::optional<Pick> pick;
            LineDrawList lines;
        };
        uint64_t serial = 0;
        std::shared_ptr<const RenderWorld> world;
        std::shared_ptr<const RayScene> irradiance_scene;
        std::vector<View> views;
        OverlayDrawList overlays;

        [[nodiscard]] bool has_visible_view() const {
            for(const auto& view : views)
                if(view.draw)
                    return true;
            return false;
        }

        [[nodiscard]] bool has_raster_view() const {
            for(const auto& view : views)
                if(view.draw && !view.parameters.path_trace)
                    return true;
            return false;
        }
    };

}

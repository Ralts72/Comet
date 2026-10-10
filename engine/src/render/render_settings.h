#pragma once

#include "graphics/enums.h"

#include <cstdint>

namespace Comet {
    struct RenderSettings {
        enum class SceneOutput { Presentation, Offscreen };
        static constexpr Format SCENE_COLOR_FORMAT = Format::R16G16B16A16_SFLOAT;
        // 由宿主在启动时选择，不从项目或开发者 Profile 读取。
        SceneOutput scene_output = SceneOutput::Presentation;
        OutputMode output_mode = OutputMode::Sdr;
        // 扩展线性输出峰值相对于 SDR 白色的倍数，不代表显示器实测能力。
        float hdr_headroom = 4.0f;
        float hdr_white_level = 1.0f;
        std::uint32_t max_frames_in_flight = 2;
        float max_anisotropy = 8.0f;
        float render_scale = 1.0f;
    };
}

#pragma once

#include "render/scene/render_submission.h"

namespace Comet {
    // 单个视图选中的具体资源与 UBO 数据；GPU 绑定仍由 MaterialRenderer 持有。
    struct COMET_API ViewLightingData {
        LightingData lighting;
        std::shared_ptr<Environment> environment;
        std::array<std::shared_ptr<Environment>, LightingData::MAX_REFLECTION_PROBES> probes;
        std::shared_ptr<Irradiance> irradiance;
        uint32_t probe_count = 0;
        uint32_t excess_probes = 0;
        uint32_t excess_irradiance_volumes = 0;
        uint32_t irradiance_probes = 0;
        uint64_t irradiance_bytes = 0;

        [[nodiscard]] static ViewLightingData prepare(
            const RenderSubmission& submission, const LightingData& lighting);
    };
}

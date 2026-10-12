#pragma once

#include "common/export.h"
#include "diagnostics/diagnostics.h"
#include "asset/import/asset_task_types.h"
#include "core/window_settings.h"
#include "graphics/vulkan_settings.h"
#include "render/render_settings.h"

namespace Comet {
    class COMET_API Config {
    public:
        using Log = LogSettings;

        using Diagnostics = DiagnosticsSettings;

        using Window = WindowSettings;
        using Vulkan = VulkanSettings;
        using Render = RenderSettings;

        Diagnostics diagnostics;
        Window window;
        Vulkan vulkan;
        Render render;
        AssetImportLimits assets;
    };
}

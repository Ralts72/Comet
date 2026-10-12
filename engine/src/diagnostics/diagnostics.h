#pragma once

#include "common/export.h"
#include "diagnostics/log_settings.h"

namespace Comet {
    struct DiagnosticsSettings {
        LogSettings log;
        bool enable_profiler = false;
        bool enable_render_diagnostics = false;
    };

    class COMET_API Diagnostics {
    public:
        explicit Diagnostics(const DiagnosticsSettings& config);

        ~Diagnostics();

        Diagnostics(const Diagnostics&) = delete;

        Diagnostics& operator=(const Diagnostics&) = delete;
    };
}

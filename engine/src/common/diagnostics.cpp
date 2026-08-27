#include "diagnostics.h"

#include "common/logger.h"
#include "common/profiler.h"

namespace Comet {
    Diagnostics::Diagnostics(const Config::Diagnostics& config) {
        const bool profiler_available = Profiler::is_available();
        const bool profiler_enabled = config.enable_profiler && profiler_available;

        Logger::init(config.log, profiler_enabled);
        Profiler::set_enabled(profiler_enabled);

        if(config.enable_profiler && !profiler_available) {
            LOG_WARN("Profiler was requested by configuration but is not available in this build");
        }
    }

    Diagnostics::~Diagnostics() {
        Profiler::dump_results();
        Profiler::set_enabled(false);
        Profiler::reset();
        Logger::shutdown();
    }
}

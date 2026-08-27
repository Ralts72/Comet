#pragma once

#include "common/config.h"
#include "common/export.h"

namespace Comet {
    class COMET_API Diagnostics {
    public:
        explicit Diagnostics(const Config::Diagnostics& config);

        ~Diagnostics();

        Diagnostics(const Diagnostics&) = delete;

        Diagnostics& operator=(const Diagnostics&) = delete;
    };
}

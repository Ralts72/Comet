#pragma once

#include "common/config.h"
#include "common/export.h"

#include <string>

namespace Comet {
    class COMET_API ConfigLoader final {
    public:
        [[nodiscard]] Config load(const std::string& config_path = {}) const;
    };
}

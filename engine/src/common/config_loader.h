#pragma once

#include "common/config.h"
#include "common/export.h"

#include <string>
#include <vector>

namespace Comet {
    class COMET_API ConfigLoader final {
    public:
        [[nodiscard]] Config load(const std::string& config_path) const;

        [[nodiscard]] Config load(const std::vector<std::string>& config_paths) const;
    };
}

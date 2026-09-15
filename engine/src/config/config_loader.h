#pragma once

#include "config/config.h"
#include "common/export.h"
#include "common/result.h"

#include <string>
#include <vector>

namespace Comet {
    class COMET_API ConfigLoader final {
    public:
        [[nodiscard]] Result<Config> load(const std::string& config_path) const;

        [[nodiscard]] Result<Config> load(const std::vector<std::string>& config_paths) const;
    };
}

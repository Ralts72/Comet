#pragma once

#include "common/result.h"
#include "common/uuid.h"

#include <filesystem>

namespace Comet {
    [[nodiscard]] COMET_API Result<std::filesystem::path> player_settings_directory(
        Uuid project_id);
}

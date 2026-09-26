#pragma once

#include "common/result.h"

#include <filesystem>

namespace CometEditor {
    [[nodiscard]] Comet::Result<std::filesystem::path> editor_user_state_directory();
}

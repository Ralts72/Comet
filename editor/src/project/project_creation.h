#pragma once

#include "common/result.h"

#include <filesystem>

namespace CometEditor {
    [[nodiscard]] Comet::Result<std::filesystem::path> create_project(
        const std::filesystem::path& root);
}

#pragma once

#include "common/result.h"

#include <filesystem>

namespace CometEditor::SystemTrash {
    [[nodiscard]] Comet::Result<void> move(const std::filesystem::path& path);
}

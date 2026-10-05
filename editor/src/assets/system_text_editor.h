#pragma once

#include "common/result.h"

#include <filesystem>

namespace CometEditor::SystemTextEditor {
    // 成功只表示已交付打开请求，不表示外部编辑或保存已经完成。
    [[nodiscard]] Comet::Result<void> open(const std::filesystem::path& path);
}

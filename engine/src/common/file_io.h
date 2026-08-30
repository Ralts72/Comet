#pragma once

#include "common/export.h"

#include <filesystem>
#include <string_view>

namespace Comet {
    COMET_API void write_text_file_atomic(
        const std::filesystem::path& path,
        std::string_view contents);
}

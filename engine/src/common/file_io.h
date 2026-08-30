#pragma once

#include "common/export.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>

namespace Comet {
    COMET_API void write_binary_file_atomic(
        const std::filesystem::path& path,
        std::span<const std::byte> contents);

    COMET_API void write_text_file_atomic(
        const std::filesystem::path& path,
        std::string_view contents);
}

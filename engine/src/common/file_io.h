#pragma once

#include "common/export.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace Comet {
    [[nodiscard]] COMET_API std::string read_text_file(const std::filesystem::path& path);

    COMET_API void write_binary_file_atomic(
        const std::filesystem::path& path, std::span<const std::byte> contents);

    COMET_API void write_text_file_atomic(
        const std::filesystem::path& path, std::string_view contents);
}

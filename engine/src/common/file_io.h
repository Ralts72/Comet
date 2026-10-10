#pragma once

#include "common/export.h"
#include "common/result.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Comet {
    [[nodiscard]] COMET_API Result<std::string> read_text_file(const std::filesystem::path& path);

    [[nodiscard]] COMET_API Result<std::vector<std::byte>> read_binary_file(
        const std::filesystem::path& path, std::size_t maximum_bytes);

    COMET_API Result<void> write_binary_file_atomic(
        const std::filesystem::path& path, std::span<const std::byte> contents);

    COMET_API Result<void> write_binary_file_atomic(
        const std::filesystem::path& path, std::span<const std::span<const std::byte>> chunks);

    COMET_API Result<void> write_text_file_atomic(
        const std::filesystem::path& path, std::string_view contents);
}

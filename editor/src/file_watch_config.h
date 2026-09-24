#pragma once

#include "common/result.h"

#include <chrono>
#include <filesystem>
#include <string_view>

namespace CometEditor {
    inline constexpr auto DEFAULT_FILE_WATCH_QUIET_PERIOD = std::chrono::milliseconds(200);

    [[nodiscard]] Comet::Result<std::chrono::milliseconds> parse_file_watch_quiet_period(
        std::string_view yaml);
    [[nodiscard]] Comet::Result<std::chrono::milliseconds> load_file_watch_quiet_period(
        const std::filesystem::path& path);
}

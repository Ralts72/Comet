#pragma once

#include <filesystem>
#include <string>

namespace Comet {
    struct LogSettings {
        bool enable_file_logging = true;
        std::string level = "trace";
        std::filesystem::path directory;
    };
}

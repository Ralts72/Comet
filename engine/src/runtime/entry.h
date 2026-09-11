#pragma once

#include "runtime.h"

#include <span>
#include <string_view>

namespace Comet {
    using ApplicationArguments = std::span<const std::string_view>;
    using ApplicationFactory = std::unique_ptr<Application> (*)(ApplicationArguments);

    COMET_API int launch(int argc, const char* const* argv, const LaunchOptions& options,
        std::string_view arguments_usage, ApplicationFactory create_application);
}

#define RUN_APP(Factory, ...)                                                            \
    int main(int argc, char** argv) {                                                    \
        return Comet::launch(argc, argv,                                                 \
            {.config_directory = COMET_CONFIG_DIRECTORY,                                 \
                .config_profile = COMET_CONFIG_PROFILE},                                 \
            std::string_view{__VA_ARGS__}, Factory);                                     \
    }

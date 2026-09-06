#pragma once

#include "runtime.h"
#include "core/project_paths.h"

#define RUN_APP(App)                                                                     \
    int main() {                                                                         \
        App app;                                                                         \
        return Comet::run(&app,                                                          \
            {.config_directory = COMET_CONFIG_DIRECTORY,                                 \
                .config_profile = COMET_CONFIG_PROFILE,                                  \
                .cache_directory = Comet::ProjectPaths(PROJECT_ROOT_DIR).cache()});      \
    }

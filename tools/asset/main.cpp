#include "asset/project_prepare.h"
#include "core/project.h"
#include "config/config_loader.h"

#include <chrono>
#include <iostream>
#include <string_view>
#include <utility>

int main(int argc, char** argv) {
    if(argc != 2 || std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: comet_prepare_project <project-directory | project.json>\n";
        return argc == 2 ? 0 : 1;
    }
    auto project = Comet::Project::load(argv[1]);
    if(!project) {
        std::cerr << project.error() << '\n';
        return 1;
    }
    const auto start = std::chrono::steady_clock::now();
    const std::filesystem::path config_directory = COMET_CONFIG_DIRECTORY;
    Comet::Config config;
    if(!std::string_view(COMET_CONFIG_PROFILE).empty()) {
        auto loaded = Comet::ConfigLoader{}.load(
            (config_directory / "profiles.json").string(), COMET_CONFIG_PROFILE);
        if(!loaded) {
            std::cerr << loaded.error() << '\n';
            return 1;
        }
        config = std::move(loaded).value();
    }
    if(auto prepared = Comet::prepare_project(project.value(), config.assets); !prepared) {
        std::cerr << "Project preparation failed: " << prepared.error() << '\n';
        return 1;
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
    std::cout << "Project assets ready: " << project.value().name() << " (" << seconds.count()
              << " s)\n";
    return 0;
}

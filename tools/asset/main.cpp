#include "asset/project_prepare.h"
#include "core/project.h"
#include "config/config_loader.h"

#include <chrono>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
    const bool help = argc == 2 && std::string_view(argv[1]) == "--help";
    const bool exporting = argc >= 4 && std::string_view(argv[2]) == "--export-assets";
    if(help || (argc != 2 && !exporting)) {
        std::cout
            << "Usage: comet_prepare_project <project-directory | project.json>\n"
               "       comet_prepare_project <project> --export-assets <new-directory> [scene ...]\n"
               "Exports startup and selected scenes with their runtime assets; excludes the executable.\n";
        return help ? 0 : 1;
    }
    auto project = Comet::Project::load(argv[1]);
    if(!project) {
        std::cerr << project.error() << '\n';
        return 1;
    }
    const auto start = std::chrono::steady_clock::now();
    Comet::Config config;
    if(!std::string_view(COMET_CONFIG_PROFILE).empty()) {
        auto loaded = Comet::ConfigLoader{}.load(COMET_CONFIG_FILE, COMET_CONFIG_PROFILE);
        if(!loaded) {
            std::cerr << loaded.error() << '\n';
            return 1;
        }
        config = std::move(loaded).value();
    }
    std::vector<std::filesystem::path> scenes;
    for(int index = 4; index < argc; ++index)
        scenes.emplace_back(argv[index]);
    auto prepared =
        exporting ? Comet::export_project_assets(project.value(), argv[3], scenes, config.assets)
                  : Comet::prepare_project(project.value(), config.assets);
    if(!prepared) {
        std::cerr << "Project preparation failed: " << prepared.error() << '\n';
        return 1;
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
    std::cout << (exporting ? "Project assets exported: " : "Project assets ready: ")
              << project.value().name() << " (" << seconds.count() << " s)\n";
    return 0;
}

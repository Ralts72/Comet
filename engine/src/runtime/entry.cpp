#include "runtime/entry.h"

#include <iostream>
#include <string_view>

namespace Comet {
    int launch(int argc, const char* const* argv, LaunchOptions options,
        ApplicationEntry run_application) {
        if(argc == 2 && std::string_view(argv[1]) == "--help") {
            std::cout << "Usage: " << std::filesystem::path(argv[0]).filename().string()
                      << " [project-directory | project.json]\n";
            return 0;
        }
        if(argc > 2 || (argc == 2 && std::string_view(argv[1]).starts_with('-'))) {
            std::cerr << "Application failed: Expected a project directory or project.json\n";
            return 1;
        }
        if(argc == 2)
            options.project_path = argv[1];
        if(auto result = run_application(options); !result) {
            std::cerr << "Application failed: " << result.error().message << '\n';
            return 1;
        }
        return 0;
    }
}

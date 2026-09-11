#include "runtime/entry.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace Comet {
    int launch(int argc, const char* const* argv, const LaunchOptions& options,
        std::string_view arguments_usage, ApplicationFactory create_application) {
        try {
            std::vector<std::string_view> arguments;
            for(int index = 1; index < argc; ++index)
                arguments.emplace_back(argv[index]);
            if(arguments.size() == 1 && arguments.front() == "--help") {
                std::cout << "Usage: "
                          << std::filesystem::path(argv[0]).filename().string();
                if(!arguments_usage.empty())
                    std::cout << ' ' << arguments_usage;
                std::cout << '\n';
                return 0;
            }
            auto app = create_application(arguments);
            if(!app)
                throw std::runtime_error("Application factory returned no application");
            return run(app.get(), options);
        } catch(const std::exception& error) {
            std::cerr << "Application failed: " << error.what() << '\n';
            return 1;
        }
    }
}

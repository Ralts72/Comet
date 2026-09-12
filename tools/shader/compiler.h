#pragma once

#include "graphics/enums.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Comet {
    class ShaderCompiler {
    public:
        enum class Target { Vulkan10, Vulkan13 };

        struct Request {
            std::filesystem::path source;
            ShaderStage stage = ShaderStage::Vertex;
            Target target = Target::Vulkan10;
            std::string entry_point = "main";
            std::map<std::string, std::string> defines;
            std::vector<std::filesystem::path> include_directories;
        };
        struct Dependency {
            std::filesystem::path path;
            std::filesystem::path resolved_path;
            std::optional<std::string> contents;
        };
        struct Result {
            std::vector<uint32_t> words;
            std::vector<Dependency> dependencies;
            std::string diagnostics;
            [[nodiscard]] bool succeeded() const { return !words.empty(); }
        };

        [[nodiscard]] static Result compile(const Request& request);
        [[nodiscard]] static bool inputs_unchanged(const Result& result);
    };
}

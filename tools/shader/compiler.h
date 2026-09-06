#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Comet {
    // 工具层 CPU 编译器；不依赖 engine、Device 或窗口。
    class ShaderCompiler {
    public:
        enum class Stage { Vertex, Fragment, Compute };
        enum class Target { Vulkan10, Vulkan13 };

        struct Request {
            std::filesystem::path source;
            Stage stage = Stage::Vertex;
            Target target = Target::Vulkan10;
            std::string entry_point = "main";
            std::map<std::string, std::string> defines;
            std::vector<std::filesystem::path> include_directories;
        };
        struct Dependency {
            std::filesystem::path path;
            std::filesystem::path resolved_path;
            // 空值记录不存在的搜索候选，后续出现同名本地头文件也会改变解析结果。
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

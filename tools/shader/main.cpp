#include "shader/compiler.h"
#include "common/file_io.h"

#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>

namespace {
    std::string depfile_path(const std::filesystem::path& path) {
        std::string escaped;
        for(const char value : path.generic_string()) {
            if(value == '\n' || value == '\r')
                throw std::invalid_argument("Newline in dependency path");
            if(value == '$')
                escaped += '$';
            if(value == ' ' || value == '#' || value == ':' || value == '\\')
                escaped += '\\';
            escaped += value;
        }
        return escaped;
    }
}

int main(int argc, char** argv) {
    try {
        Comet::ShaderCompiler::Request request;
        std::filesystem::path output;
        std::filesystem::path depfile;
        bool has_stage = false;
        for(int index = 1; index < argc; ++index) {
            const std::string_view option(argv[index]);
            if(index + 1 == argc)
                throw std::invalid_argument(
                    "Missing option value: " + std::string(option));
            const std::string value(argv[++index]);
            if(option == "--source")
                request.source = value;
            else if(option == "--output")
                output = value;
            else if(option == "--depfile")
                depfile = value;
            else if(option == "--entry")
                request.entry_point = value;
            else if(option == "--include")
                request.include_directories.emplace_back(value);
            else if(option == "--define") {
                const auto separator = value.find('=');
                const auto name = value.substr(0, separator);
                std::string definition = "1";
                if(separator != std::string::npos)
                    definition = value.substr(separator + 1);
                if(!request.defines.emplace(name, definition).second)
                    throw std::invalid_argument("Duplicate define: " + name);
            } else if(option == "--stage") {
                has_stage = true;
                if(value == "vert")
                    request.stage = Comet::ShaderStage::Vertex;
                else if(value == "frag")
                    request.stage = Comet::ShaderStage::Fragment;
                else if(value == "comp")
                    request.stage = Comet::ShaderStage::Compute;
                else
                    throw std::invalid_argument("Unsupported stage: " + value);
            } else if(option == "--target") {
                if(value == "vulkan1.0")
                    request.target = Comet::ShaderCompiler::Target::Vulkan10;
                else if(value == "vulkan1.3")
                    request.target = Comet::ShaderCompiler::Target::Vulkan13;
                else
                    throw std::invalid_argument("Unsupported target: " + value);
            } else
                throw std::invalid_argument("Unknown option: " + std::string(option));
        }
        if(request.source.empty() || output.empty() || !has_stage)
            throw std::invalid_argument(
                "Required: --source FILE --stage vert|frag|comp --output FILE");
        const auto result = Comet::ShaderCompiler::compile(request);
        if(!result.diagnostics.empty())
            std::cerr << result.diagnostics << '\n';
        if(!result.succeeded())
            return 1;
        if(!depfile.empty()) {
            std::string dependencies = depfile_path(output) + ":";
            for(const auto& dependency : result.dependencies) {
                if(dependency.contents)
                    dependencies += " " + depfile_path(dependency.path);
            }
            dependencies += '\n';
            Comet::write_text_file_atomic(depfile, dependencies);
        }
        Comet::write_binary_file_atomic(output, std::as_bytes(std::span(result.words)));
        return 0;
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

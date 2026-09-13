#include "shader/compiler.h"
#include "common/file_io.h"

#include <iostream>
#include <span>
#include <exception>
#include <optional>
#include <string_view>

namespace {
    int fail(std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

    std::optional<std::string> depfile_path(const std::filesystem::path& path) {
        std::string escaped;
        for(const char value : path.generic_string()) {
            if(value == '\n' || value == '\r')
                return std::nullopt;
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
                return fail("Missing option value: " + std::string(option));
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
                    return fail("Duplicate define: " + name);
            } else if(option == "--stage") {
                has_stage = true;
                if(value == "vert")
                    request.stage = Comet::ShaderStage::Vertex;
                else if(value == "frag")
                    request.stage = Comet::ShaderStage::Fragment;
                else if(value == "comp")
                    request.stage = Comet::ShaderStage::Compute;
                else
                    return fail("Unsupported stage: " + value);
            } else if(option == "--target") {
                if(value == "vulkan1.0")
                    request.target = Comet::ShaderCompiler::Target::Vulkan10;
                else if(value == "vulkan1.3")
                    request.target = Comet::ShaderCompiler::Target::Vulkan13;
                else
                    return fail("Unsupported target: " + value);
            } else
                return fail("Unknown option: " + std::string(option));
        }
        if(request.source.empty() || output.empty() || !has_stage)
            return fail("Required: --source FILE --stage vert|frag|comp --output FILE");
        const auto result = Comet::ShaderCompiler::compile(request);
        if(!result.diagnostics.empty())
            std::cerr << result.diagnostics << '\n';
        if(!result.succeeded())
            return 1;
        if(!depfile.empty()) {
            const auto escaped_output = depfile_path(output);
            if(!escaped_output)
                return fail("Newline in dependency path");
            std::string dependencies = *escaped_output + ":";
            for(const auto& dependency : result.dependencies) {
                if(dependency.contents) {
                    const auto escaped = depfile_path(dependency.path);
                    if(!escaped)
                        return fail("Newline in dependency path");
                    dependencies += " " + *escaped;
                }
            }
            dependencies += '\n';
            if(auto saved = Comet::write_text_file_atomic(depfile, dependencies); !saved)
                return fail(saved.error());
        }
        if(auto saved =
                Comet::write_binary_file_atomic(output, std::as_bytes(std::span(result.words)));
            !saved)
            return fail(saved.error());
        return 0;
    } catch(const std::exception& error) {
        return fail(error.what());
    }
}

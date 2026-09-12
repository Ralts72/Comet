#include "shader/compiler.h"

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <exception>
#include <system_error>
#include <utility>

namespace Comet {
    namespace {
        constexpr size_t MAX_SOURCE_BYTES = 8 * 1024 * 1024;
        constexpr size_t MAX_SOURCE_FILES = 256;
        constexpr size_t MAX_INCLUDE_DEPTH = 64;

        std::optional<std::string> read_source(
            const std::filesystem::path& path, std::string& error) {
            error.clear();
            std::error_code code;
            const bool exists = std::filesystem::exists(path, code);
            if(code) {
                error = "Cannot inspect shader source: " + path.string() + ": "
                        + code.message();
                return std::nullopt;
            }
            if(!exists)
                return std::nullopt;
            const auto size = std::filesystem::file_size(path, code);
            if(code) {
                error =
                    "Cannot read shader source: " + path.string() + ": " + code.message();
                return std::nullopt;
            }
            if(size > MAX_SOURCE_BYTES) {
                error = "Shader source exceeds 8 MiB: " + path.string();
                return std::nullopt;
            }
            std::ifstream input(path, std::ios::binary);
            if(!input) {
                error = "Cannot read shader source: " + path.string();
                return std::nullopt;
            }
            std::string contents(static_cast<size_t>(size), '\0');
            input.read(contents.data(), static_cast<std::streamsize>(size));
            if(!input || input.peek() != std::char_traits<char>::eof()) {
                error = "Shader source changed while reading: " + path.string();
                return std::nullopt;
            }
            return contents;
        }

        bool identifier(const std::string& name) {
            if(name.empty())
                return false;
            const auto first = static_cast<unsigned char>(name.front());
            if(first != '_' && !std::isalpha(first))
                return false;
            return std::ranges::all_of(name,
                [](unsigned char value) { return value == '_' || std::isalnum(value); });
        }

        struct GlslangProcess {
            bool initialized = glslang::InitializeProcess();
            ~GlslangProcess() {
                if(initialized)
                    glslang::FinalizeProcess();
            }
        };

        class SourceIncluder final: public glslang::TShader::Includer {
        public:
            explicit SourceIncluder(const ShaderCompiler::Request& request)
                : m_directories(request.include_directories) {}

            const std::string* read(const std::filesystem::path& input) {
                if(!m_error.empty())
                    return nullptr;
                std::error_code code;
                const auto logical =
                    std::filesystem::absolute(input, code).lexically_normal();
                if(code) {
                    m_error = "Cannot resolve shader path: " + input.string() + ": "
                              + code.message();
                    return nullptr;
                }
                const auto path = std::filesystem::weakly_canonical(logical, code);
                if(code) {
                    m_error = "Cannot resolve shader path: " + logical.string() + ": "
                              + code.message();
                    return nullptr;
                }
                if(!m_resolutions.contains(logical)
                    && m_resolutions.size() >= MAX_SOURCE_FILES) {
                    m_error = "Shader include search exceeds 256 files";
                    return nullptr;
                }
                const auto [resolution, inserted] = m_resolutions.emplace(logical, path);
                if(!inserted && resolution->second != path) {
                    m_error = "Shader input path changed during compilation: "
                              + logical.string();
                    return nullptr;
                }
                const auto found = m_sources.find(path);
                if(found != m_sources.end())
                    return found->second ? &*found->second : nullptr;
                auto contents = read_source(path, m_error);
                if(!m_error.empty())
                    return nullptr;
                if(contents) {
                    m_total_bytes += contents->size();
                    if(m_total_bytes > MAX_SOURCE_BYTES) {
                        m_error = "Shader input snapshot exceeds 8 MiB";
                        return nullptr;
                    }
                }
                const auto& stored =
                    m_sources.emplace(path, std::move(contents)).first->second;
                return stored ? &*stored : nullptr;
            }

            IncludeResult* includeLocal(
                const char* header, const char* source, size_t depth) override {
                return include(
                    std::filesystem::path(source).parent_path() / header, depth);
            }
            IncludeResult* includeSystem(
                const char* header, const char*, size_t depth) override {
                for(const auto& directory : m_directories) {
                    if(auto* result = include(directory / header, depth))
                        return result;
                    if(!m_error.empty())
                        break;
                }
                return nullptr;
            }
            void releaseInclude(IncludeResult* result) override { delete result; }
            [[nodiscard]] std::vector<ShaderCompiler::Dependency> dependencies() const {
                std::vector<ShaderCompiler::Dependency> result;
                for(const auto& [logical, path] : m_resolutions) {
                    if(const auto found = m_sources.find(path); found != m_sources.end())
                        result.push_back({logical, path, found->second});
                }
                return result;
            }
            [[nodiscard]] const std::string& error() const { return m_error; }

        private:
            IncludeResult* include(const std::filesystem::path& path, size_t depth) {
                // Keep exceptions inside the callback boundary.
                try {
                    if(depth > MAX_INCLUDE_DEPTH) {
                        m_error = "Shader include depth exceeds 64";
                        return nullptr;
                    }
                    const auto* contents = read(path);
                    if(!contents)
                        return nullptr;
                    std::error_code code;
                    const auto logical =
                        std::filesystem::absolute(path, code).lexically_normal();
                    if(code) {
                        m_error = "Cannot resolve shader path: " + path.string() + ": "
                                  + code.message();
                        return nullptr;
                    }
                    return new IncludeResult(logical.generic_string(), contents->data(),
                        contents->size(), nullptr);
                } catch(const std::exception& error) {
                    m_error = error.what();
                    return nullptr;
                }
            }
            std::vector<std::filesystem::path> m_directories;
            std::map<std::filesystem::path, std::optional<std::string>> m_sources;
            std::map<std::filesystem::path, std::filesystem::path> m_resolutions;
            size_t m_total_bytes = 0;
            std::string m_error;
        };

        ShaderCompiler::Result compile_source(
            const ShaderCompiler::Request& request, SourceIncluder& includer) {
            static const GlslangProcess process;
            if(!process.initialized)
                return {.diagnostics = "Cannot initialize glslang"};
            if(!identifier(request.entry_point))
                return {.diagnostics = "Invalid Shader entry point"};
            std::string preamble;
            for(const auto& [name, value] : request.defines) {
                if(!identifier(name) || value.find_first_of("\r\n") != std::string::npos
                    || value.find('\0') != std::string::npos)
                    return {.diagnostics = "Invalid Shader define: " + name};
                preamble += "#define " + name + " " + value + "\n";
            }
            EShLanguage stage;
            switch(request.stage) {
                case ShaderStage::Vertex:
                    stage = EShLangVertex;
                    break;
                case ShaderStage::Fragment:
                    stage = EShLangFragment;
                    break;
                case ShaderStage::Compute:
                    stage = EShLangCompute;
                    break;
                default:
                    return {.diagnostics = "Unsupported Shader stage"};
            }
            std::error_code code;
            const auto source_path =
                std::filesystem::absolute(request.source, code).lexically_normal();
            if(code)
                return {.diagnostics = "Cannot resolve shader path: "
                                       + request.source.string() + ": " + code.message()};
            const auto* source = includer.read(source_path);
            if(!source) {
                if(!includer.error().empty())
                    return {};
                return {
                    .diagnostics = "Shader source is missing: " + source_path.string()};
            }
            const auto source_name = source_path.generic_string();
            const char* name = source_name.c_str();
            const char* contents = source->c_str();
            const int length = static_cast<int>(source->size());
            glslang::TShader shader(stage);
            shader.setStringsWithLengthsAndNames(&contents, &length, &name, 1);
            shader.setPreamble(preamble.c_str());
            shader.setEntryPoint(request.entry_point.c_str());
            shader.setSourceEntryPoint("main");
            shader.setEnvInput(
                glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
            switch(request.target) {
                case ShaderCompiler::Target::Vulkan10:
                    shader.setEnvClient(
                        glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
                    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
                    break;
                case ShaderCompiler::Target::Vulkan13:
                    shader.setEnvClient(
                        glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
                    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
                    break;
                default:
                    return {.diagnostics = "Unsupported Shader target"};
            }
            const auto messages =
                static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
            if(!shader.parse(GetDefaultResources(), 450, false, messages, includer)) {
                return {.diagnostics =
                            std::string(shader.getInfoLog()) + shader.getInfoDebugLog()};
            }
            glslang::TProgram program;
            program.addShader(&shader);
            if(!program.link(messages)) {
                return {.diagnostics = std::string(program.getInfoLog())
                                       + program.getInfoDebugLog()};
            }
            ShaderCompiler::Result result;
            glslang::SpvOptions options;
            options.disableOptimizer = true;
            spv::SpvBuildLogger logger;
            glslang::GlslangToSpv(
                *program.getIntermediate(stage), result.words, &logger, &options);
            result.diagnostics =
                std::string(shader.getInfoLog()) + logger.getAllMessages();
            return result;
        }
    }

    bool ShaderCompiler::inputs_unchanged(const Result& result) {
        return std::ranges::all_of(result.dependencies, [](const auto& dependency) {
            std::error_code code;
            const auto resolved =
                std::filesystem::weakly_canonical(dependency.path, code);
            if(code || resolved != dependency.resolved_path)
                return false;
            std::string error;
            const auto contents = read_source(resolved, error);
            return error.empty() && contents == dependency.contents;
        });
    }

    ShaderCompiler::Result ShaderCompiler::compile(const Request& request) {
        Result result;
        SourceIncluder includer(request);
        try {
            result = compile_source(request, includer);
        } catch(const std::exception& error) {
            result.words.clear();
            result.diagnostics = error.what();
        }
        result.dependencies = includer.dependencies();
        if(!includer.error().empty()) {
            result.words.clear();
            if(!result.diagnostics.empty())
                result.diagnostics += '\n';
            result.diagnostics += includer.error();
        }
        if(result.succeeded() && !inputs_unchanged(result)) {
            result.words.clear();
            result.diagnostics =
                "Shader inputs changed during compilation: " + request.source.string();
        }
        return result;
    }
}

#include "asset/script.h"
#include "asset/data/script_sources.h"
#include "scripting/script_compiler.h"

#include <algorithm>

namespace Comet {
    using namespace ScriptSource;

    Result<std::shared_ptr<Script>, Error> Script::create(std::string source, std::string name) {
        if(source.size() > MAX_SOURCE_BYTES)
            return Result<std::shared_ptr<Script>, Error>::failure({"Script exceeds 1 MiB limit"});
        auto script = std::make_shared<Script>();
        script->m_source = std::move(source);
        script->m_name = std::move(name);
        if(auto prepared = prepare_script_definition(*script); !prepared)
            return Result<std::shared_ptr<Script>, Error>::failure(prepared.error());
        return Result<std::shared_ptr<Script>, Error>::success(std::move(script));
    }

    Result<std::shared_ptr<Script>, Error> Script::load(const std::filesystem::path& path) {
        std::error_code error;
        const auto resolved = std::filesystem::weakly_canonical(path, error);
        if(is_module_path(path) || (!error && is_module_path(resolved)))
            return Result<std::shared_ptr<Script>, Error>::failure(
                {"Module sources cannot be attached as scripts: " + path.string()});
        auto source = read_source(path);
        if(!source)
            return Result<std::shared_ptr<Script>, Error>::failure({source.error()});
        return create(std::move(source).value(), path.string());
    }

    Result<std::vector<std::shared_ptr<Script>>, Script::LoadFailure> Script::load_group(
        const std::filesystem::path& assets_root,
        const std::span<const std::filesystem::path> relative_paths) {
        using Loaded = Result<std::vector<std::shared_ptr<Script>>, LoadFailure>;
        LoadFailure failure;
        if(relative_paths.empty() || relative_paths.size() > MAX_GROUP_ROOTS)
            return Loaded::failure({"Script group needs between 1 and 128 roots", {}});
        std::error_code error;
        auto sources = std::make_shared<ScriptSources>();
        sources->root = std::filesystem::canonical(assets_root, error);
        if(error || !std::filesystem::is_directory(sources->root, error) || error)
            return Loaded::failure({"Cannot resolve project assets directory", {}});
        failure.m_sources = sources;
        std::vector<std::shared_ptr<Script>> scripts;
        const auto fail = [&](std::string message) {
            if(failure.message.empty())
                failure.message = std::move(message);
        };
        for(const auto& input : relative_paths) {
            if(!safe_source_path(input) || is_module_path(input)) {
                fail("Invalid component script path: " + input.generic_string());
                continue;
            }
            const auto path = input.lexically_normal();
            if(failure.dependencies.contains(path)) {
                fail("Duplicate script root: " + path.generic_string());
                continue;
            }
            auto& dependencies = failure.dependencies[path];
            const auto captured = sources->capture(path);
            if(!captured) {
                fail(captured.error());
                continue;
            }
            if(is_module_path(captured.value()->resolved)) {
                fail("Module sources cannot be attached as scripts: " + path.generic_string());
                continue;
            }
            auto script = std::make_shared<Script>();
            script->m_sources = sources;
            script->m_source_path = path;
            script->m_name = path.generic_string();
            const auto prepared = prepare_script_definition(*script, sources.get(), &dependencies);
            std::ranges::sort(dependencies);
            if(!prepared) {
                fail(prepared.error().message);
                continue;
            }
            script->m_dependencies = dependencies;
            scripts.push_back(std::move(script));
        }
        if(!failure.message.empty())
            return Loaded::failure(std::move(failure));
        if(!sources->inputs_are_current()) {
            failure.message = "Script sources changed during preparation";
            return Loaded::failure(std::move(failure));
        }
        return Loaded::success(std::move(scripts));
    }
}

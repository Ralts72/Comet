#include "assets/shader_program_import.h"
#include "asset/import/import_service.h"
#include "asset/serialization/shader_program_serializer.h"
#include "shader/compiler.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace CometEditor {
    namespace {
        using Import = ShaderProgramImport;

        Comet::Result<Import::Source> resolve_source(const Comet::AssetDatabase& database,
            const Comet::ProjectPaths& paths, const Comet::ShaderProgramStage& stage,
            const std::string_view extension) {
            const auto* record = database.find(stage.source);
            auto actual_extension = record ? record->path.extension().string() : std::string{};
            std::ranges::transform(actual_extension, actual_extension.begin(),
                [](const unsigned char character) { return char(std::tolower(character)); });
            if(!record || record->type != Comet::AssetType::Shader || actual_extension != extension)
                return Comet::Result<Import::Source>::failure(
                    "Shader program source handle does not reference a " + std::string(extension)
                    + " asset: " + std::to_string(stage.source.value()));
            return Comet::Result<Import::Source>::success({stage.source,
                database.get_revision(stage.source), paths.assets() / record->path, stage.entry});
        }

        bool contains_path(const Comet::ImportInputSnapshot& snapshot,
            const std::filesystem::path& root, const std::filesystem::path& path) {
            std::error_code error;
            const auto canonical_root = std::filesystem::weakly_canonical(root, error);
            if(error)
                return false;
            const auto canonical_path = std::filesystem::weakly_canonical(path, error);
            if(error)
                return false;
            const auto relative = canonical_path.lexically_relative(canonical_root);
            return std::ranges::any_of(
                snapshot.files, [&](const auto& file) { return file.relative_path == relative; });
        }

    }

    Comet::Result<ShaderProgramImport::Request> ShaderProgramImport::resolve(
        const Comet::AssetDatabase& database, const Comet::ProjectPaths& paths,
        const Comet::AssetHandle handle) {
        const auto* record = database.find(handle);
        if(!record || record->type != Comet::AssetType::ShaderProgram)
            return Comet::Result<Request>::failure("Shader program is not indexed");
        const auto descriptor_path = paths.assets() / record->path;
        auto descriptor = Comet::ShaderProgramSerializer{}.load(descriptor_path);
        if(!descriptor)
            return Comet::Result<Request>::failure(descriptor.error());
        auto vertex = resolve_source(database, paths, descriptor.value().vertex, ".vert");
        if(!vertex)
            return Comet::Result<Request>::failure(vertex.error());
        auto fragment = resolve_source(database, paths, descriptor.value().fragment, ".frag");
        if(!fragment)
            return Comet::Result<Request>::failure(fragment.error());
        return Comet::Result<Request>::success({handle, database.get_revision(handle),
            std::move(vertex).value(), std::move(fragment).value(), descriptor_path,
            std::move(descriptor).value().material});
    }

    Comet::Result<ShaderProgramImport::Candidate, ShaderProgramImport::Failure>
    ShaderProgramImport::prepare(const Comet::ProjectPaths& paths, const Request& request) {
        using Prepared = Comet::Result<Candidate, Failure>;
        if(auto cached = Comet::ShaderProgramArtifact::load(
               Comet::ImportService(paths).shader_program_artifact_path(request.handle),
               request.handle);
            cached
            && cached->inputs.files.front().relative_path
                   == request.descriptor_path.lexically_relative(paths.assets())
            && contains_path(cached->inputs, paths.assets(), request.vertex.path)
            && contains_path(cached->inputs, paths.assets(), request.fragment.path)
            && Comet::import_inputs_are_current(paths.assets(), cached->inputs))
            return Prepared::success({std::move(*cached), true});

        std::vector<std::filesystem::path> watched{request.vertex.path,
            Comet::metadata_path(request.vertex.path), request.fragment.path,
            Comet::metadata_path(request.fragment.path),
            Comet::metadata_path(request.descriptor_path)};
        const auto observe = [&](const Comet::ShaderCompiler::Result& result) {
            for(const auto& dependency : result.dependencies)
                watched.push_back(dependency.path);
        };

        const auto compile_stage = [&](const Source& source, const Comet::ShaderStage stage) {
            return Comet::ShaderCompiler::compile({.source = source.path,
                .stage = stage,
                .target = Comet::ShaderCompiler::Target::Vulkan13,
                .entry_point = source.entry});
        };
        auto vertex = compile_stage(request.vertex, Comet::ShaderStage::Vertex);
        observe(vertex);
        if(!vertex.succeeded())
            return Prepared::failure({"Vertex shader: " + vertex.diagnostics, std::move(watched)});
        auto fragment = compile_stage(request.fragment, Comet::ShaderStage::Fragment);
        observe(fragment);
        if(!fragment.succeeded())
            return Prepared::failure(
                {"Fragment shader: " + fragment.diagnostics, std::move(watched)});
        if(!Comet::ShaderCompiler::inputs_unchanged(vertex)
            || !Comet::ShaderCompiler::inputs_unchanged(fragment))
            return Prepared::failure(
                {"Shader inputs changed during compilation", std::move(watched)});

        std::vector<std::filesystem::path> dependencies{request.vertex.path,
            Comet::metadata_path(request.vertex.path), request.fragment.path,
            Comet::metadata_path(request.fragment.path),
            Comet::metadata_path(request.descriptor_path)};
        for(const auto* result : {&vertex, &fragment})
            for(const auto& dependency : result->dependencies)
                if(dependency.contents)
                    dependencies.push_back(dependency.resolved_path);
        auto inputs =
            Comet::capture_import_inputs(paths.assets(), request.descriptor_path, dependencies);
        if(!inputs)
            return Prepared::failure({inputs.error(), std::move(watched)});
        if(!Comet::ShaderCompiler::inputs_unchanged(vertex)
            || !Comet::ShaderCompiler::inputs_unchanged(fragment)
            || !Comet::import_inputs_are_current(paths.assets(), inputs.value()))
            return Prepared::failure(
                {"Shader inputs changed during compilation", std::move(watched)});
        return Prepared::success(
            {{request.handle, std::move(inputs).value(), std::move(vertex.words),
                 std::move(fragment.words), request.vertex.entry, request.fragment.entry,
                 request.material},
                false});
    }

}

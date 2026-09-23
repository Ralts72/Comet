#include "assets/shader_program_import.h"
#include "asset/serialization/shader_program_serializer.h"
#include "core/task_scheduler.h"
#include "diagnostics/logger.h"
#include "shader/compiler.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
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

        bool request_is_current(
            const Comet::AssetDatabase& database, const Import::Request& request) {
            if(!database.is_current(request.handle, request.revision)
                || !database.is_current(request.vertex.handle, request.vertex.revision)
                || !database.is_current(request.fragment.handle, request.fragment.revision))
                return false;
            const auto* program = database.find(request.handle);
            const auto* vertex = database.find(request.vertex.handle);
            const auto* fragment = database.find(request.fragment.handle);
            return program && program->type == Comet::AssetType::ShaderProgram && vertex
                   && vertex->type == Comet::AssetType::Shader && fragment
                   && fragment->type == Comet::AssetType::Shader;
        }
    }

    std::filesystem::path ShaderProgramImport::artifact_path(
        const Comet::ProjectPaths& paths, const Comet::AssetHandle handle) {
        return paths.cache() / "shaders" / (std::to_string(handle.value()) + ".csp");
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
            std::move(vertex).value(), std::move(fragment).value(), descriptor_path});
    }

    Comet::Result<ShaderProgramImport::Candidate, ShaderProgramImport::Failure>
    ShaderProgramImport::prepare(const Comet::ProjectPaths& paths, const Request& request) {
        using Prepared = Comet::Result<Candidate, Failure>;
        if(auto cached = Comet::ShaderProgramArtifact::load(
               artifact_path(paths, request.handle), request.handle);
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
        return Prepared::success({{request.handle, std::move(inputs).value(),
                                      std::move(vertex.words), std::move(fragment.words)},
            false});
    }

    Comet::Result<std::shared_ptr<const Comet::ShaderProgramArtifact>> ShaderProgramImport::publish(
        const Comet::AssetDatabase& database, const Comet::ProjectPaths& paths,
        const Request& request, Candidate candidate) {
        using Result = Comet::Result<std::shared_ptr<const Comet::ShaderProgramArtifact>>;
        if(!request_is_current(database, request)
            || !Comet::import_inputs_are_current(paths.assets(), candidate.artifact.inputs))
            return Result::failure("Shader program candidate is stale");
        if(candidate.artifact.handle != request.handle)
            return Result::failure("Shader program candidate identity mismatch");
        if(!candidate.from_cache) {
            if(auto saved = candidate.artifact.publish_atomic(artifact_path(paths, request.handle));
                !saved)
                return Result::failure(saved.error());
        }
        return Result::success(
            std::make_shared<const Comet::ShaderProgramArtifact>(std::move(candidate.artifact)));
    }

    ShaderProgramImportService::ShaderProgramImportService(Comet::AssetManager& manager,
        const Comet::ProjectPaths& paths, Comet::TaskScheduler& scheduler)
        : m_manager(manager), m_paths(paths), m_scheduler(scheduler) {}

    void ShaderProgramImportService::accept_scan(const Comet::AssetScanReport& report) {
        if(!report.snapshot_updated)
            return;
        std::unordered_set<Comet::AssetHandle> changed;
        changed.insert(report.added_assets.begin(), report.added_assets.end());
        changed.insert(report.modified_assets.begin(), report.modified_assets.end());
        const auto& database = m_manager.get_database();
        database.include_dependents(changed);
        for(const auto handle : changed) {
            const auto* record = database.find(handle);
            if(record && record->type == Comet::AssetType::ShaderProgram)
                m_pending.insert(handle);
        }
        for(const auto handle : report.removed_assets) {
            m_pending.erase(handle);
            m_compiled.erase(handle);
        }
    }

    void ShaderProgramImportService::update() {
        complete();
        schedule();
    }

    std::shared_ptr<const Comet::ShaderProgramArtifact> ShaderProgramImportService::
        compiled_program(const Comet::AssetHandle handle) const {
        const auto found = m_compiled.find(handle);
        return found != m_compiled.end() ? found->second : nullptr;
    }

    void ShaderProgramImportService::complete() {
        const auto& database = m_manager.get_database();
        for(auto task = m_tasks.begin(); task != m_tasks.end();) {
            if(task->completion.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                ++task;
                continue;
            }
            task->completion.get();
            const auto request = task->request;
            auto result = std::move(task->result->value());
            task = m_tasks.erase(task);
            if(!result) {
                std::vector<std::filesystem::path> dependencies(
                    database.get_import_dependencies(request.handle).begin(),
                    database.get_import_dependencies(request.handle).end());
                dependencies.insert(dependencies.end(), result.error().dependencies.begin(),
                    result.error().dependencies.end());
                if(database.is_current(request.handle, request.revision)) {
                    if(auto indexed = m_manager.update_import_dependencies(
                           request.handle, std::move(dependencies));
                        !indexed)
                        LOG_WARN("Shader program {} dependencies: {}", request.handle.value(),
                            indexed.error());
                }
                LOG_WARN("Shader program {}: {}", request.handle.value(), result.error().message);
                continue;
            }
            auto published =
                ShaderProgramImport::publish(database, m_paths, request, std::move(result).value());
            if(!published) {
                LOG_WARN("Shader program {}: {}", request.handle.value(), published.error());
                continue;
            }
            std::vector<std::filesystem::path> dependencies;
            for(const auto& file : published.value()->inputs.files)
                if(file.relative_path != published.value()->inputs.files.front().relative_path)
                    dependencies.push_back(file.relative_path);
            if(auto indexed =
                    m_manager.update_import_dependencies(request.handle, std::move(dependencies));
                !indexed) {
                LOG_WARN(
                    "Shader program {} dependencies: {}", request.handle.value(), indexed.error());
                continue;
            }
            m_compiled[request.handle] = std::move(published).value();
        }
    }

    void ShaderProgramImportService::schedule() {
        constexpr std::size_t MAX_IN_FLIGHT = 2;
        const auto& database = m_manager.get_database();
        for(auto pending = m_pending.begin();
            pending != m_pending.end() && m_tasks.size() < MAX_IN_FLIGHT;) {
            const auto handle = *pending;
            const auto* record = database.find(handle);
            if(!record || record->type != Comet::AssetType::ShaderProgram) {
                pending = m_pending.erase(pending);
                continue;
            }
            if(std::ranges::any_of(
                   m_tasks, [&](const Task& task) { return task.request.handle == handle; })) {
                ++pending;
                continue;
            }
            auto request = ShaderProgramImport::resolve(database, m_paths, handle);
            if(!request) {
                LOG_WARN("Shader program {}: {}", handle.value(), request.error());
                pending = m_pending.erase(pending);
                continue;
            }
            auto output = std::make_shared<std::optional<
                Comet::Result<ShaderProgramImport::Candidate, ShaderProgramImport::Failure>>>();
            auto future =
                m_scheduler.try_submit([paths = m_paths, input = request.value(), output] {
                    try {
                        *output = ShaderProgramImport::prepare(paths, input);
                    } catch(const std::exception& error) {
                        *output = Comet::Result<ShaderProgramImport::Candidate,
                            ShaderProgramImport::Failure>::failure({error.what(), {}});
                    }
                });
            if(!future)
                break;
            m_tasks.push_back({std::move(request).value(), std::move(output), std::move(*future)});
            pending = m_pending.erase(pending);
        }
    }
}

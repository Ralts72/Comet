#pragma once

#include "asset/artifact/shader_program_artifact.h"
#include "asset/asset_manager.h"
#include "asset/database.h"
#include "asset/data/shader_program_data.h"
#include "core/project_paths.h"

#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CometEditor {
    class ShaderProgramImport final {
    public:
        struct Source {
            Comet::AssetHandle handle;
            Comet::AssetRevision revision = 0;
            std::filesystem::path path;
            std::string entry;
        };

        struct Request {
            Comet::AssetHandle handle;
            Comet::AssetRevision revision = 0;
            Source vertex;
            Source fragment;
            std::filesystem::path descriptor_path;
        };

        struct Candidate {
            Comet::ShaderProgramArtifact artifact;
            bool from_cache = false;
        };

        struct Failure {
            std::string message;
            std::vector<std::filesystem::path> dependencies;
        };

        [[nodiscard]] static Comet::Result<Request> resolve(const Comet::AssetDatabase& database,
            const Comet::ProjectPaths& paths, Comet::AssetHandle handle);
        // Worker-safe: consumes only copied paths/identities; never touches the database or GPU.
        [[nodiscard]] static Comet::Result<Candidate, Failure> prepare(
            const Comet::ProjectPaths& paths, const Request& request);
        // Owner thread validates all identities and inputs before making the CPU version visible.
        [[nodiscard]] static Comet::Result<std::shared_ptr<const Comet::ShaderProgramArtifact>>
        publish(const Comet::AssetDatabase& database, const Comet::ProjectPaths& paths,
            const Request& request, Candidate candidate);

        [[nodiscard]] static std::filesystem::path artifact_path(
            const Comet::ProjectPaths& paths, Comet::AssetHandle handle);
    };

    class ShaderProgramImportService final {
    public:
        ShaderProgramImportService(Comet::AssetManager& manager, const Comet::ProjectPaths& paths,
            Comet::TaskScheduler& scheduler);

        void accept_scan(const Comet::AssetScanReport& report);
        void update();
        [[nodiscard]] std::shared_ptr<const Comet::ShaderProgramArtifact> compiled_program(
            Comet::AssetHandle handle) const;

    private:
        struct Task {
            ShaderProgramImport::Request request;
            std::shared_ptr<std::optional<
                Comet::Result<ShaderProgramImport::Candidate, ShaderProgramImport::Failure>>>
                result;
            std::future<void> completion;
        };

        void complete();
        void schedule();

        Comet::AssetManager& m_manager;
        const Comet::ProjectPaths& m_paths;
        Comet::TaskScheduler& m_scheduler;
        std::unordered_set<Comet::AssetHandle> m_pending;
        std::vector<Task> m_tasks;
        std::unordered_map<Comet::AssetHandle, std::shared_ptr<const Comet::ShaderProgramArtifact>>
            m_compiled;
    };
}

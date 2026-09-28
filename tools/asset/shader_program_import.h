#pragma once

#include "asset/artifact/shader_program_artifact.h"
#include "asset/database.h"
#include "asset/data/shader_program_data.h"
#include "asset/import/import_candidate.h"
#include "core/project_paths.h"

#include <filesystem>

namespace Comet {
    class ShaderProgramImport final {
    public:
        using Source = Comet::ShaderProgramImportSource;
        using Request = Comet::ShaderProgramImportRequest;
        using Candidate = Comet::ShaderProgramImportPrepared;
        using Failure = Comet::ShaderProgramImportFailure;

        [[nodiscard]] static Comet::Result<Request> resolve(
            const Comet::AssetDatabase& database, Comet::AssetHandle handle);
        // Worker 只消费已复制的路径和身份，不访问数据库或 GPU。
        [[nodiscard]] static Comet::Result<Candidate, Failure> prepare(
            const Comet::ProjectPaths& paths, const Request& request);
    };
}

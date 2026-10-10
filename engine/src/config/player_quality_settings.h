#pragma once

#include "render/quality_settings.h"
#include "common/uuid.h"

#include <filesystem>
#include <functional>

namespace Comet {
    class COMET_API PlayerQualitySettings final {
    public:
        [[nodiscard]] static Result<PlayerQualitySettings> load(
            Uuid project_id, QualitySettings defaults);
        [[nodiscard]] static Result<PlayerQualitySettings> load(
            Uuid project_id, QualitySettings defaults, const std::filesystem::path& path);
        [[nodiscard]] const QualitySettings& settings() const { return m_settings; }
        [[nodiscard]] const std::filesystem::path& path() const { return m_path; }
        [[nodiscard]] Result<void> save(QualitySettings settings);
        [[nodiscard]] Result<void> save_and_apply(QualitySettings settings,
            const std::function<Result<void>(const QualitySettings&)>& apply);

    private:
        Uuid m_project_id;
        QualitySettings m_settings;
        std::filesystem::path m_path;
    };
}

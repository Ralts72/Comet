#pragma once

#include "config/display_settings.h"
#include "common/uuid.h"

#include <filesystem>
#include <functional>

namespace Comet {
    class COMET_API PlayerDisplaySettings final {
    public:
        [[nodiscard]] static Result<PlayerDisplaySettings> load(
            Uuid project_id, DisplaySettings defaults);
        [[nodiscard]] static Result<PlayerDisplaySettings> load(
            Uuid project_id, DisplaySettings defaults, const std::filesystem::path& path);
        [[nodiscard]] const DisplaySettings& settings() const { return m_settings; }
        [[nodiscard]] const std::filesystem::path& path() const { return m_path; }
        [[nodiscard]] Result<void> save(DisplaySettings settings);
        [[nodiscard]] Result<void> save_and_apply(DisplaySettings settings,
            const std::function<Result<void>(const DisplaySettings&)>& apply);

    private:
        Uuid m_project_id;
        DisplaySettings m_settings;
        std::filesystem::path m_path;
    };
}

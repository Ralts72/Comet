#pragma once

#include "input/input_overrides.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Comet {
    class COMET_API PlayerInputSettings final {
    public:
        [[nodiscard]] static Result<PlayerInputSettings> load(Uuid project_id);
        [[nodiscard]] static Result<PlayerInputSettings> load(
            Uuid project_id, const std::filesystem::path& path);
        [[nodiscard]] static Result<std::filesystem::path> default_path(Uuid project_id);

        [[nodiscard]] const InputOverrides& overrides() const { return m_overrides; }
        [[nodiscard]] const std::filesystem::path& path() const { return m_path; }
        [[nodiscard]] Result<void> save(InputOverrides overrides);

    private:
        PlayerInputSettings(Uuid project_id, std::filesystem::path path);

        Uuid m_project_id;
        std::filesystem::path m_path;
        std::optional<std::string> m_source_contents;
        InputOverrides m_overrides;
    };
}

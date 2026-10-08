#pragma once

#include "input/input_overrides.h"
#include "common/error.h"

#include <filesystem>
#include <functional>

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
        [[nodiscard]] Result<void> save_and_apply(const InputActions& defaults,
            InputOverrides overrides,
            const std::function<Result<void, Error>(InputActions)>& apply_actions);

    private:
        PlayerInputSettings(Uuid project_id, std::filesystem::path path);

        Uuid m_project_id;
        std::filesystem::path m_path;
        InputOverrides m_overrides;
    };
}

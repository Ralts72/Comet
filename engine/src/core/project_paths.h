#pragma once

#include "common/export.h"

#include <filesystem>

namespace Comet {
    class COMET_API ProjectPaths final {
    public:
        explicit ProjectPaths(std::filesystem::path root);

        [[nodiscard]] const std::filesystem::path& root() const noexcept;
        [[nodiscard]] std::filesystem::path assets() const;
        [[nodiscard]] std::filesystem::path local_data() const;
        [[nodiscard]] std::filesystem::path cache() const;
        [[nodiscard]] std::filesystem::path editor_state() const;
        [[nodiscard]] std::filesystem::path settings() const;

    private:
        std::filesystem::path m_root;
    };
}

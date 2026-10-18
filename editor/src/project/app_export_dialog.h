#pragma once

#include "project/app_export.h"
#include <optional>
#include <string>
#include <span>

namespace CometEditor {
    class AppExportDialog {
    public:
        void open(const std::filesystem::path& project, const std::filesystem::path& startup,
            std::vector<std::filesystem::path> scenes,
            std::span<const std::filesystem::path> included = {});
        void render(bool scene_modified, bool edit_mode);
        std::optional<AppExportRequest> take_request();
        void append_output(std::string_view output);
        void complete(const Comet::Result<void>& result);

    private:
        struct SceneChoice {
            std::filesystem::path path;
            bool selected = false;
            bool required = false;
        };
        std::filesystem::path m_project;
        std::filesystem::path m_startup;
        std::string m_destination;
        std::vector<SceneChoice> m_scenes;
        std::optional<AppExportRequest> m_request;
        std::string m_message;
        std::string m_log;
        bool m_open_log = false;
        bool m_open_requested = false;
        bool m_busy = false;
    };
}

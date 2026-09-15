#pragma once
#include "common/error.h"
#include "common/result.h"
#include <array>
#include <filesystem>
#include <optional>
#include <string>
namespace CometEditor {
    class SceneFileDialog {
    public:
        enum class Action { None, Open, Save };
        struct Request {
            Action action;
            std::string path;
        };
        void request(Action action, const std::filesystem::path& current_path,
            const std::filesystem::path& scene_directory);
        void render();
        [[nodiscard]] std::optional<Request> take_request();
        void complete(const Comet::Result<void, Comet::Error>& result);

    private:
        Action m_action = Action::None;
        bool m_open_requested = false;
        bool m_close_requested = false;
        std::optional<Request> m_request;
        std::string m_error;
        std::array<char, 1024> m_path_buffer{};
    };
}

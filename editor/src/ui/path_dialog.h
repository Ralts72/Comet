#pragma once
#include "common/error.h"
#include "common/result.h"
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
namespace CometEditor {
    class PathDialog {
    public:
        enum class Action { None, OpenScene, SaveScene, OpenProject };
        struct Request {
            Action action;
            std::string path;
        };
        void request(Action action, const std::filesystem::path& current_path,
            const std::filesystem::path& default_directory);
        void render();
        [[nodiscard]] std::optional<Request> take_request();
        void complete(const Comet::Result<void, Comet::Error>& result);
        [[nodiscard]] bool take_cancelled() { return std::exchange(m_cancelled, false); }

    private:
        Action m_action = Action::None;
        bool m_open_requested = false;
        bool m_close_requested = false;
        bool m_cancelled = false;
        std::optional<Request> m_request;
        std::string m_error;
        std::array<char, 1024> m_path_buffer{};
    };
}

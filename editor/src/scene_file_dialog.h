#pragma once
#include <array>
#include <filesystem>
namespace CometEditor {
    class SceneDocument;
    class SceneFileDialog {
    public:
        enum class Action { None, Open, Save };
        void request(Action action, SceneDocument& document,
            const std::filesystem::path& project_root);
        [[nodiscard]] bool render(SceneDocument& document);

    private:
        Action m_action = Action::None;
        bool m_open_requested = false;
        std::array<char, 1024> m_path_buffer{};
    };
}

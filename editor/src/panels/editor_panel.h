#pragma once
#include <string>

namespace CometEditor {

    class EditorPanel {
    public:
        explicit EditorPanel(const std::string& name) : m_name(name) {}
        virtual ~EditorPanel() = default;

        virtual void render() = 0;

        [[nodiscard]] const std::string& get_name() const { return m_name; }

        void set_visible(const bool visible) { m_user_visible = visible; }
        [[nodiscard]] bool is_open() const { return m_user_visible; }
        void toggle_visible() { m_user_visible = !m_user_visible; }

    protected:
        std::string m_name;
        bool m_user_visible = true;
    };

}

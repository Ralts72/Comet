#pragma once
#include "pch.h"

namespace CometEditor {

    class EditorPanel {
    public:
        explicit EditorPanel(const std::string& name) : m_name(name) {}
        virtual ~EditorPanel() = default;

        virtual void render() = 0;

        [[nodiscard]] const std::string& get_name() const { return m_name; }

        [[nodiscard]] bool is_visible() const { return m_actually_visible; }

        void set_visible(const bool visible) { m_user_visible = visible; }
        void toggle_visible() { m_user_visible = !m_user_visible; }

    protected:
        std::string m_name;
        bool m_user_visible = true;  // 用户设置的可见性
        mutable bool m_actually_visible = false;  // 实际可见性
    };

}


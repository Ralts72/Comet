#pragma once
#include "pch.h"

namespace CometEditor {

    class EditorPanel {
    public:
        explicit EditorPanel(const std::string& name) : m_name(name), m_visible(true) {}
        virtual ~EditorPanel() = default;

        virtual void render() = 0;

        [[nodiscard]] const std::string& get_name() const { return m_name; }
        [[nodiscard]] bool is_visible() const { return m_visible; }
        void set_visible(const bool visible) { m_visible = visible; }
        void toggle_visible() { m_visible = !m_visible; }

    protected:
        std::string m_name;
        bool m_visible;
    };

}


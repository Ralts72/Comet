#pragma once

#include "scene/component_registry.h"

#include <functional>
#include <unordered_map>
#include <utility>

namespace Comet {
    class AssetDatabase;
}

namespace CometEditor {
    struct PropertyEditResult {
        bool changed = false;
        bool active = false;
        bool began = false;
        bool finished = false;
        [[nodiscard]] static PropertyEditResult from_item(bool changed);
    };

    class PropertyEditorRegistry {
    public:
        using PropertyEditor =
            std::function<PropertyEditResult(const Comet::PropertyDescriptor&, void*)>;

        [[nodiscard]] bool register_editor(
            const Comet::PropertyType type, PropertyEditor editor) {
            if(!editor) {
                return false;
            }
            return m_editors.emplace(type, std::move(editor)).second;
        }

        [[nodiscard]] PropertyEditResult edit_property(
            const Comet::PropertyDescriptor& property, void* value) const {
            if(value == nullptr || !property.editable || property.read_only)
                return {};
            const auto editor = m_editors.find(property.type);
            if(editor == m_editors.end())
                return {};
            return editor->second(property, value);
        }

        [[nodiscard]] bool contains(const Comet::PropertyType type) const {
            return m_editors.contains(type);
        }

    private:
        std::unordered_map<Comet::PropertyType, PropertyEditor> m_editors;
    };

    [[nodiscard]] PropertyEditorRegistry create_property_editor_registry(
        const Comet::AssetDatabase& database);
}

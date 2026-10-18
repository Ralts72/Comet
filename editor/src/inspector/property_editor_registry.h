#pragma once

#include "reflection/type.h"
#include "asset/script.h"

#include <functional>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

namespace Comet {
    class AssetDatabase;
    class Scene;
}

namespace CometEditor {
    struct PropertyEditResult {
        bool changed = false;
        bool active = false;
        bool began = false;
        bool finished = false;
        uint32_t active_item = 0;
        [[nodiscard]] static PropertyEditResult from_item(bool changed);
        void include_item(bool item_changed);
    };

    struct ParameterEditResult: PropertyEditResult {
        std::vector<std::string> changed_parameters;
    };

    class PropertyEditorRegistry {
    public:
        using PropertyEditor =
            std::function<PropertyEditResult(const Comet::PropertyDescriptor&, void*)>;

        [[nodiscard]] bool register_editor(const Comet::PropertyType type, PropertyEditor editor) {
            if(!editor) {
                return false;
            }
            return m_editors.emplace(type, std::move(editor)).second;
        }

        using CollectionEditor = std::function<PropertyEditResult(const PropertyEditorRegistry&,
            const Comet::PropertyDescriptor&, Comet::PropertyCollection&, Comet::Scene*,
            std::optional<std::uint64_t>)>;
        void set_collection_editor(CollectionEditor editor) {
            m_collection_editor = std::move(editor);
        }

        [[nodiscard]] PropertyEditResult edit_property(const Comet::PropertyDescriptor& property,
            void* value, Comet::Scene* scene = nullptr,
            std::optional<std::uint64_t> drop_generation = std::nullopt) const {
            if(!value || !property.editable || property.read_only)
                return {};
            if(compound(property.type))
                return m_collection_editor ? m_collection_editor(*this, property,
                                                 *static_cast<Comet::PropertyCollection*>(value),
                                                 scene, drop_generation)
                                           : PropertyEditResult{};
            const auto editor = m_editors.find(property.type);
            return editor != m_editors.end() ? editor->second(property, value)
                                             : PropertyEditResult{};
        }
        [[nodiscard]] bool contains(Comet::PropertyType type) const {
            return compound(type) ? bool(m_collection_editor) : m_editors.contains(type);
        }
        [[nodiscard]] ParameterEditResult edit_parameters(
            const Comet::Script::PropertyMap& properties, Comet::ParameterMap& overrides,
            Comet::Scene& scene, std::optional<std::uint64_t> drop_generation = std::nullopt,
            std::span<const Comet::ParameterMap* const> peers = {}) const;

    private:
        static bool compound(Comet::PropertyType type) {
            return type == Comet::PropertyType::Object || type == Comet::PropertyType::Sequence
                   || type == Comet::PropertyType::Optional;
        }
        CollectionEditor m_collection_editor;
        std::unordered_map<Comet::PropertyType, PropertyEditor> m_editors;
    };

    [[nodiscard]] PropertyEditorRegistry create_property_editor_registry(
        const Comet::AssetDatabase& database);
}

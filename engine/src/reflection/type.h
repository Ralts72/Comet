#pragma once

#include "asset/reference.h"
#include "common/export.h"
#include "common/parameters.h"
#include "scene/entity_uuid.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <span>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Comet {
    enum class PropertyType {
        Bool,
        Float,
        Vec3,
        Vec4,
        AssetHandle,
        String,
        Enum,
        Parameters,
        EntityReference,
        UInt32,
        Int32,
        Object,
        Sequence,
        Optional
    };

    // 复合属性快照拥有独立内容，复制和比较均按值，撤销与 Prefab 不共享可变子节点。
    class COMET_API PropertyCollection {
    public:
        enum class Kind { Object, Sequence, Optional };
        struct Data;
        PropertyCollection();
        explicit PropertyCollection(Data data);
        ~PropertyCollection();
        PropertyCollection(const PropertyCollection&);
        PropertyCollection& operator=(const PropertyCollection&);
        PropertyCollection(PropertyCollection&&) noexcept;
        PropertyCollection& operator=(PropertyCollection&&) noexcept;
        [[nodiscard]] const Data& data() const;
        [[nodiscard]] Data& data();
        [[nodiscard]] bool operator==(const PropertyCollection&) const;

    private:
        std::unique_ptr<Data> m_data;
    };

    using PropertyValue = std::variant<bool, float, Math::Vec3, Math::Vec4, AssetHandle,
        std::string, ParameterMap, EntityUuid, uint32_t, int32_t, PropertyCollection>;

    struct PropertyCollection::Data {
        Kind kind = Kind::Object;
        std::vector<std::pair<std::string, PropertyValue>> fields;
        std::vector<PropertyValue> elements;
        bool operator==(const Data&) const = default;
    };

    template<typename Value, typename Visitor>
        requires std::is_same_v<std::remove_const_t<Value>, PropertyValue>
    void visit_entity_references(Value& value, Visitor&& visitor) {
        if(auto* reference = std::get_if<EntityUuid>(&value)) {
            visitor(*reference);
        } else if(auto* parameters = std::get_if<ParameterMap>(&value)) {
            for(auto& [name, parameter] : *parameters)
                if(auto* reference = std::get_if<EntityUuid>(&parameter))
                    visitor(*reference);
        } else if(auto* collection = std::get_if<PropertyCollection>(&value)) {
            for(auto& [name, child] : collection->data().fields)
                visit_entity_references(child, visitor);
            for(auto& child : collection->data().elements)
                visit_entity_references(child, visitor);
        }
    }

    struct NumericPropertyMetadata {
        float speed = 0.1f;
        std::optional<float> minimum;
        std::optional<float> maximum;
        bool enforce_bounds = false;
    };

    struct PropertyMetadata {
        bool editable = true;
        bool serializable = true;
        bool transient = false;
        bool read_only = false;
        bool required = true;
        bool omit_default = false;
        bool bitmask = false;
        NumericPropertyMetadata numeric;
        std::optional<AssetType> asset_type;
        bool asset_cpu_only = false;
        std::string group;
        std::string description;
        std::size_t max_elements = 4096;
    };

    struct TypeDescriptor;
    struct PropertyDescriptor {
        enum class WriteMode { Edit, Restore };
        struct EnumOption {
            std::string id;
            std::string display_name;
        };
        [[nodiscard]] COMET_API std::optional<PropertyValue> copy_value(const void* object) const;
        [[nodiscard]] COMET_API bool accepts_value(const PropertyValue& value) const;
        [[nodiscard]] COMET_API bool assign_value(
            void* object, const PropertyValue& value, WriteMode mode = WriteMode::Edit) const;
        [[nodiscard]] COMET_API bool valid(std::size_t depth = 0) const;
        [[nodiscard]] COMET_API bool has_entity_references() const;
        COMET_API void collect_asset_references(
            const PropertyValue&, std::vector<AssetReference>&) const;

        std::string id;
        std::string display_name;
        PropertyType type = PropertyType::Float;
        bool editable = true;
        bool serializable = true;
        bool transient = false;
        bool read_only = false;
        bool required = true;
        bool omit_default = false;
        bool bitmask = false;
        NumericPropertyMetadata numeric;
        std::optional<AssetType> asset_type;
        bool asset_cpu_only = false;
        std::string group;
        std::string description;
        std::function<void*(void*)> mutable_accessor;
        std::function<const void*(const void*)> const_accessor;
        std::function<void(void*)> on_changed;
        std::vector<EnumOption> enum_options;
        std::function<std::optional<std::string>(const void*)> read_enum;
        std::function<bool(void*, std::string_view)> write_enum;
        std::vector<PropertyDescriptor> members;
        std::shared_ptr<const PropertyDescriptor> element;
        std::optional<std::size_t> fixed_size;
        std::size_t max_elements = 4096;
        std::function<std::optional<PropertyValue>(const void*)> copy_callback;
        std::function<bool(void*, const PropertyValue&, WriteMode)> assign_callback;
        std::function<PropertyValue()> default_value;

        [[nodiscard]] void* get_value(void* object) const {
            return object && mutable_accessor ? mutable_accessor(object) : nullptr;
        }
        [[nodiscard]] const void* get_value(const void* object) const {
            return object && const_accessor ? const_accessor(object) : nullptr;
        }
        void notify_changed(void* value) const {
            if(value && on_changed)
                on_changed(value);
        }
    };

    struct COMET_API TypeDescriptor {
        std::string id;
        std::string display_name;
        std::vector<PropertyDescriptor> properties;
        [[nodiscard]] const PropertyDescriptor* find_property(std::string_view id) const;
        // 点分字段与 [index]；Optional 的内容在存在时透明访问。
        [[nodiscard]] std::optional<PropertyDescriptor> resolve_property(
            std::string_view path) const;
        [[nodiscard]] bool valid() const;
    };

}

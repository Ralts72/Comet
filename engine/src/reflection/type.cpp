#include "reflection/type.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <unordered_set>

namespace Comet {
    PropertyCollection::PropertyCollection() : m_data(std::make_unique<Data>()) {}
    PropertyCollection::PropertyCollection(Data data)
        : m_data(std::make_unique<Data>(std::move(data))) {}
    PropertyCollection::~PropertyCollection() = default;
    PropertyCollection::PropertyCollection(const PropertyCollection& other)
        : m_data(std::make_unique<Data>(other.data())) {}
    PropertyCollection& PropertyCollection::operator=(const PropertyCollection& other) {
        if(this != &other)
            m_data = std::make_unique<Data>(other.data());
        return *this;
    }
    PropertyCollection::PropertyCollection(PropertyCollection&&) noexcept = default;
    PropertyCollection& PropertyCollection::operator=(PropertyCollection&&) noexcept = default;
    const PropertyCollection::Data& PropertyCollection::data() const {
        if(m_data)
            return *m_data;
        static const Data empty;
        return empty;
    }
    PropertyCollection::Data& PropertyCollection::data() {
        if(!m_data)
            m_data = std::make_unique<Data>();
        return *m_data;
    }
    bool PropertyCollection::operator==(const PropertyCollection& other) const {
        return data() == other.data();
    }

    std::optional<PropertyValue> PropertyDescriptor::copy_value(const void* object) const {
        const auto* value = get_value(object);
        if(!value)
            return std::nullopt;
        if(copy_callback)
            return copy_callback(value);
        switch(type) {
            case PropertyType::Bool:
                return *static_cast<const bool*>(value);
            case PropertyType::Float:
                return *static_cast<const float*>(value);
            case PropertyType::Int32:
                return *static_cast<const int32_t*>(value);
            case PropertyType::UInt32:
                return *static_cast<const uint32_t*>(value);
            case PropertyType::Vec3:
                return *static_cast<const Math::Vec3*>(value);
            case PropertyType::Vec4:
                return *static_cast<const Math::Vec4*>(value);
            case PropertyType::AssetHandle:
                return *static_cast<const AssetHandle*>(value);
            case PropertyType::String:
                return *static_cast<const std::string*>(value);
            case PropertyType::Parameters:
                return *static_cast<const ParameterMap*>(value);
            case PropertyType::EntityReference:
                return *static_cast<const EntityUuid*>(value);
            case PropertyType::Enum:
                if(read_enum)
                    if(auto name = read_enum(value))
                        return PropertyValue(*name);
                return std::nullopt;
            case PropertyType::Object:
            case PropertyType::Sequence:
            case PropertyType::Optional:
                return std::nullopt;
        }
        return std::nullopt;
    }

    bool PropertyDescriptor::accepts_value(const PropertyValue& value) const {
        const auto in_bounds = [this](double number) {
            return !numeric.enforce_bounds
                   || ((!numeric.minimum || number >= *numeric.minimum)
                       && (!numeric.maximum || number <= *numeric.maximum));
        };
        return std::visit(
            [&](const auto& source) {
                using T = std::remove_cvref_t<decltype(source)>;
                if constexpr(std::is_same_v<T, bool>)
                    return type == PropertyType::Bool;
                else if constexpr(std::is_same_v<T, uint32_t>)
                    return type == PropertyType::UInt32 && in_bounds(source);
                else if constexpr(std::is_same_v<T, int32_t>)
                    return type == PropertyType::Int32 && in_bounds(source);
                else if constexpr(std::is_same_v<T, float>)
                    return type == PropertyType::Float && std::isfinite(source)
                           && in_bounds(source);
                else if constexpr(std::is_same_v<T, Math::Vec3>)
                    return type == PropertyType::Vec3 && Math::is_finite(source)
                           && in_bounds(source.x) && in_bounds(source.y) && in_bounds(source.z);
                else if constexpr(std::is_same_v<T, Math::Vec4>)
                    return type == PropertyType::Vec4 && Math::is_finite(source)
                           && in_bounds(source.x) && in_bounds(source.y) && in_bounds(source.z)
                           && in_bounds(source.w);
                else if constexpr(std::is_same_v<T, std::string>) {
                    return type == PropertyType::String
                           || (type == PropertyType::Enum
                               && std::ranges::any_of(enum_options,
                                   [&](const auto& option) { return option.id == source; }));
                } else if constexpr(std::is_same_v<T, ParameterMap>)
                    return type == PropertyType::Parameters && valid_parameters(source);
                else if constexpr(std::is_same_v<T, EntityUuid>)
                    return type == PropertyType::EntityReference;
                else if constexpr(std::is_same_v<T, AssetHandle>)
                    return type == PropertyType::AssetHandle;
                else {
                    const auto& data = source.data();
                    if(type == PropertyType::Object) {
                        if(data.kind != PropertyCollection::Kind::Object || !data.elements.empty())
                            return false;
                        std::unordered_set<std::string_view> seen;
                        for(const auto& [name, child] : data.fields) {
                            const auto member =
                                std::ranges::find(members, name, &PropertyDescriptor::id);
                            if(member == members.end() || !seen.insert(name).second
                                || !member->accepts_value(child))
                                return false;
                        }
                        return std::ranges::all_of(members, [&](const auto& member) {
                            return !member.required || !member.serializable || member.transient
                                   || seen.contains(member.id);
                        });
                    }
                    if(!data.fields.empty() || !element)
                        return false;
                    if(type == PropertyType::Sequence) {
                        if(data.kind != PropertyCollection::Kind::Sequence
                            || data.elements.size() > max_elements
                            || (fixed_size && data.elements.size() != *fixed_size))
                            return false;
                    } else if(type == PropertyType::Optional) {
                        if(data.kind != PropertyCollection::Kind::Optional
                            || data.elements.size() > 1)
                            return false;
                    } else
                        return false;
                    return std::ranges::all_of(data.elements,
                        [&](const auto& child) { return element->accepts_value(child); });
                }
            },
            value);
    }

    bool PropertyDescriptor::assign_value(
        void* object, const PropertyValue& value, WriteMode mode) const {
        void* destination = get_value(object);
        if(!destination || !accepts_value(value)
            || (mode == WriteMode::Edit && (!editable || read_only)))
            return false;
        bool assigned = false;
        if(assign_callback)
            assigned = assign_callback(destination, value, mode);
        else if(type == PropertyType::Enum) {
            const auto* name = std::get_if<std::string>(&value);
            assigned = name && write_enum && write_enum(destination, *name);
        } else
            assigned = std::visit(
                [destination](const auto& source) {
                    using T = std::remove_cvref_t<decltype(source)>;
                    if constexpr(std::is_same_v<T, PropertyCollection>)
                        return false;
                    else {
                        *static_cast<T*>(destination) = source;
                        return true;
                    }
                },
                value);
        if(assigned && mode == WriteMode::Edit)
            notify_changed(destination);
        return assigned;
    }

    bool PropertyDescriptor::valid(std::size_t depth) const {
        if(depth > 32 || id.empty() || id.find_first_of(".[]") != std::string::npos
            || display_name.empty() || !mutable_accessor || !const_accessor
            || (transient && serializable)
            || (numeric.enforce_bounds && !numeric.minimum && !numeric.maximum)
            || (numeric.minimum && !std::isfinite(*numeric.minimum))
            || (numeric.maximum && !std::isfinite(*numeric.maximum))
            || (numeric.minimum && numeric.maximum && *numeric.minimum > *numeric.maximum)
            || (asset_type && *asset_type == AssetType::Unknown)
            || (asset_cpu_only && asset_type != AssetType::Mesh))
            return false;
        if(type == PropertyType::Enum) {
            if(enum_options.empty() || !read_enum || !write_enum)
                return false;
            std::unordered_set<std::string_view> ids;
            for(const auto& option : enum_options)
                if(option.id.empty() || option.display_name.empty()
                    || !ids.insert(option.id).second)
                    return false;
        } else if(!enum_options.empty() || read_enum || write_enum)
            return false;
        if(type == PropertyType::Object) {
            if(!copy_callback || !assign_callback || members.empty() || asset_type)
                return false;
            std::unordered_set<std::string_view> ids;
            for(const auto& member : members)
                if(!ids.insert(member.id).second || !member.valid(depth + 1))
                    return false;
            return true;
        }
        if(type == PropertyType::Sequence || type == PropertyType::Optional)
            return copy_callback && assign_callback && element && max_elements != 0
                   && (!fixed_size || *fixed_size <= max_elements) && element->valid(depth + 1);
        return static_cast<unsigned>(type) <= static_cast<unsigned>(PropertyType::Optional)
               && (!asset_type || type == PropertyType::AssetHandle);
    }

    bool PropertyDescriptor::has_entity_references() const {
        return type == PropertyType::EntityReference || type == PropertyType::Parameters
               || std::ranges::any_of(
                   members, [](const auto& member) { return member.has_entity_references(); })
               || (element && element->has_entity_references());
    }

    void PropertyDescriptor::collect_asset_references(
        const PropertyValue& value, std::vector<AssetReference>& references) const {
        if(const auto* handle = std::get_if<AssetHandle>(&value); handle && *handle && asset_type)
            references.push_back({*handle, *asset_type, true, asset_cpu_only});
        if(const auto* collection = std::get_if<PropertyCollection>(&value)) {
            if(type == PropertyType::Object) {
                for(const auto& [name, child] : collection->data().fields) {
                    const auto found = std::ranges::find(members, name, &PropertyDescriptor::id);
                    if(found != members.end())
                        found->collect_asset_references(child, references);
                }
            } else if(element)
                for(const auto& child : collection->data().elements)
                    element->collect_asset_references(child, references);
        }
    }

    const PropertyDescriptor* TypeDescriptor::find_property(std::string_view name) const {
        const auto found = std::ranges::find(properties, name, &PropertyDescriptor::id);
        return found != properties.end() ? &*found : nullptr;
    }
    bool TypeDescriptor::valid() const {
        if(id.empty() || display_name.empty())
            return false;
        std::unordered_set<std::string_view> ids;
        for(const auto& property : properties)
            if(!ids.insert(property.id).second || !property.valid())
                return false;
        return true;
    }

    namespace {
        using PathStep = std::variant<std::string, std::size_t>;
        std::optional<std::vector<PathStep>> parse_path(std::string_view path) {
            if(path.empty() || path.front() == '.' || path.front() == '[')
                return std::nullopt;
            std::vector<PathStep> steps;
            std::size_t position = 0;
            while(position < path.size()) {
                if(path[position] == '[') {
                    const auto end = path.find(']', position + 1);
                    if(end == std::string_view::npos)
                        return std::nullopt;
                    std::size_t index = 0;
                    const auto parsed =
                        std::from_chars(path.data() + position + 1, path.data() + end, index);
                    if(parsed.ec != std::errc{} || parsed.ptr != path.data() + end
                        || parsed.ptr == path.data() + position + 1)
                        return std::nullopt;
                    steps.emplace_back(index);
                    position = end + 1;
                } else {
                    if(path[position] == '.') {
                        ++position;
                        if(position == path.size() || path[position] == '.'
                            || path[position] == '[')
                            return std::nullopt;
                    }
                    auto end = path.find_first_of(".[", position);
                    if(end == std::string_view::npos)
                        end = path.size();
                    if(end == position)
                        return std::nullopt;
                    steps.emplace_back(std::string(path.substr(position, end - position)));
                    position = end;
                }
                if(position < path.size() && path[position] != '.' && path[position] != '[')
                    return std::nullopt;
            }
            return steps;
        }
        template<typename Value>
        Value* descend(
            Value& value, const PropertyDescriptor& root, std::span<const PathStep> steps) {
            Value* current = &value;
            const PropertyDescriptor* shape = &root;
            for(const auto& step : steps) {
                while(shape->type == PropertyType::Optional) {
                    auto* collection = std::get_if<PropertyCollection>(current);
                    if(!collection || collection->data().elements.empty())
                        return nullptr;
                    current = &collection->data().elements.front();
                    shape = shape->element.get();
                }
                auto* collection = std::get_if<PropertyCollection>(current);
                if(!collection)
                    return nullptr;
                if(const auto* name = std::get_if<std::string>(&step)) {
                    if(shape->type != PropertyType::Object)
                        return nullptr;
                    auto& fields = collection->data().fields;
                    const auto field = std::ranges::find_if(
                        fields, [&](const auto& field) { return field.first == *name; });
                    const auto descriptor =
                        std::ranges::find(shape->members, *name, &PropertyDescriptor::id);
                    if(field == fields.end() || descriptor == shape->members.end())
                        return nullptr;
                    current = &field->second;
                    shape = &*descriptor;
                } else {
                    const auto index = std::get<std::size_t>(step);
                    if(shape->type != PropertyType::Sequence
                        || index >= collection->data().elements.size())
                        return nullptr;
                    current = &collection->data().elements[index];
                    shape = shape->element.get();
                }
            }
            return current;
        }
    }
    std::optional<PropertyDescriptor> TypeDescriptor::resolve_property(
        std::string_view path) const {
        if(const auto* direct = find_property(path))
            return *direct;
        auto steps = parse_path(path);
        if(!steps || steps->size() < 2 || !std::holds_alternative<std::string>(steps->front()))
            return std::nullopt;
        const auto* root = find_property(std::get<std::string>(steps->front()));
        if(!root)
            return std::nullopt;
        PropertyDescriptor leaf = *root;
        bool editable = leaf.editable && !leaf.read_only;
        for(std::size_t index = 1; index < steps->size(); ++index) {
            while(leaf.type == PropertyType::Optional && leaf.element) {
                auto child = *leaf.element;
                leaf = std::move(child);
                editable &= leaf.editable && !leaf.read_only;
            }
            const auto& step = (*steps)[index];
            if(const auto* name = std::get_if<std::string>(&step)) {
                const auto found = std::ranges::find(leaf.members, *name, &PropertyDescriptor::id);
                if(leaf.type != PropertyType::Object || found == leaf.members.end())
                    return std::nullopt;
                // Copy before assigning: found points into leaf's existing member storage.
                auto child = *found;
                leaf = std::move(child);
            } else {
                if(leaf.type != PropertyType::Sequence || !leaf.element)
                    return std::nullopt;
                auto child = *leaf.element;
                leaf = std::move(child);
            }
            editable &= leaf.editable && !leaf.read_only;
        }
        leaf.id = std::string(path);
        leaf.editable = editable;
        leaf.mutable_accessor = root->mutable_accessor;
        leaf.const_accessor = root->const_accessor;
        leaf.on_changed = {};
        auto shape = *root;
        shape.mutable_accessor = [](void* value) { return value; };
        shape.const_accessor = [](const void* value) { return value; };
        auto tail = std::vector<PathStep>(steps->begin() + 1, steps->end());
        leaf.copy_callback = [shape, tail](const void* value) -> std::optional<PropertyValue> {
            auto snapshot = shape.copy_value(value);
            if(!snapshot)
                return std::nullopt;
            const auto* child = descend(*snapshot, shape, tail);
            return child ? std::optional<PropertyValue>(*child) : std::nullopt;
        };
        leaf.assign_callback = [shape, tail](void* value, const PropertyValue& replacement,
                                   PropertyDescriptor::WriteMode mode) {
            auto snapshot = shape.copy_value(value);
            if(!snapshot)
                return false;
            auto* child = descend(*snapshot, shape, tail);
            if(!child)
                return false;
            *child = replacement;
            return shape.assign_value(value, *snapshot, mode);
        };
        return leaf;
    }
}

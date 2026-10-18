#pragma once

#include "reflection/type.h"

#include "reflection/entt_backend.h"

namespace Comet {
    template<auto Member> struct ReflectedMember {};
    namespace ReflectionDetail {
        template<typename T> struct MemberTraits;
        template<typename Object, typename Value> struct MemberTraits<Value Object::*> {
            using Owner = Object;
            using Type = Value;
        };
        template<auto Member> using MemberOwner = typename MemberTraits<decltype(Member)>::Owner;
        template<auto Member> using MemberValue = typename MemberTraits<decltype(Member)>::Type;

        template<auto Member> void bind_member(PropertyDescriptor& property) {
            using Object = MemberOwner<Member>;
            using Value = MemberValue<Member>;
            const auto id = identifier(property.id);
            if(!entt::resolve<Object>(context()).data(id))
                entt::meta_factory<Object>{context()}.template data<Member, entt::as_ref_t>(id);
            // EnTT 的成员节点位于可扩容容器中；只保存类型与 ID，访问时重新定位节点。
            property.mutable_accessor = [id](void* object) -> void* {
                auto instance = entt::forward_as_meta(context(), *static_cast<Object*>(object));
                auto value = entt::resolve<Object>(context()).data(id).get(instance);
                return value.template try_cast<Value>();
            };
            property.const_accessor = [id](const void* object) -> const void* {
                auto instance =
                    entt::forward_as_meta(context(), *static_cast<const Object*>(object));
                const auto value = entt::resolve<Object>(context()).data(id).get(instance);
                return value.template try_cast<Value>();
            };
        }

        template<typename T> void publish_type(const TypeDescriptor& description) {
            if(!description.valid())
                return;
            const auto native = entt::resolve<T>(context());
            if(static_cast<const TypeMetadata*>(native.custom()))
                return;
            entt::meta_factory<T>{context()}
                .type(identifier(description.id))
                .template custom<TypeMetadata>(description,
                    std::is_default_constructible_v<T> && std::is_copy_constructible_v<T>);
        }

        template<typename Enum, std::size_t N>
        void bind_enumeration(const std::pair<Enum, PropertyDescriptor::EnumOption> (&options)[N]) {
            if(static_cast<const EnumMetadata*>(entt::resolve<Enum>(context()).custom()))
                return;
            EnumMetadata metadata;
            metadata.values.reserve(N);
            for(const auto& [value, option] : options)
                metadata.values.emplace_back(option.id, entt::meta_any(context(), value));
            entt::meta_factory<Enum>{context()}.template custom<EnumMetadata>(std::move(metadata));
        }

        template<typename T> struct Sequence {
            static constexpr bool value = false;
        };
        template<typename T, typename A> struct Sequence<std::vector<T, A>> {
            static constexpr bool value = true;
            static constexpr bool fixed = false;
            using Element = T;
        };
        template<typename T, std::size_t N> struct Sequence<std::array<T, N>> {
            static constexpr bool value = true;
            static constexpr bool fixed = true;
            static constexpr std::size_t size = N;
            using Element = T;
        };
        template<typename T> struct Optional {
            static constexpr bool value = false;
        };
        template<typename T> struct Optional<std::optional<T>> {
            static constexpr bool value = true;
            using Element = T;
        };

        inline void apply_metadata(PropertyDescriptor& property, PropertyMetadata metadata) {
            property.editable = metadata.editable;
            property.serializable = metadata.serializable;
            property.transient = metadata.transient;
            property.read_only = metadata.read_only;
            property.required = metadata.required;
            property.omit_default = metadata.omit_default;
            property.bitmask = metadata.bitmask;
            property.numeric = std::move(metadata.numeric);
            property.asset_type = metadata.asset_type;
            property.asset_cpu_only = metadata.asset_cpu_only;
            property.group = std::move(metadata.group);
            property.description = std::move(metadata.description);
            property.max_elements = metadata.max_elements;
            if(property.element) {
                auto element = std::make_shared<PropertyDescriptor>(*property.element);
                PropertyMetadata child;
                child.numeric = property.numeric;
                child.bitmask = property.bitmask;
                child.asset_type = property.asset_type;
                child.asset_cpu_only = property.asset_cpu_only;
                apply_metadata(*element, child);
                property.element = std::move(element);
            }
        }

        template<typename T>
        PropertyDescriptor describe_value(const TypeDescriptor* schema = nullptr) {
            PropertyDescriptor property{.id = "value",
                .display_name = "值",
                .mutable_accessor = [](void* value) { return value; },
                .const_accessor = [](const void* value) { return value; }};
            if constexpr(std::is_same_v<T, bool>)
                property.type = PropertyType::Bool;
            else if constexpr(std::is_same_v<T, uint32_t>)
                property.type = PropertyType::UInt32;
            else if constexpr(std::is_same_v<T, int32_t>)
                property.type = PropertyType::Int32;
            else if constexpr(std::is_same_v<T, float>)
                property.type = PropertyType::Float;
            else if constexpr(std::is_same_v<T, Math::Vec3>)
                property.type = PropertyType::Vec3;
            else if constexpr(std::is_same_v<T, Math::Vec4>)
                property.type = PropertyType::Vec4;
            else if constexpr(std::is_same_v<T, AssetHandle>)
                property.type = PropertyType::AssetHandle;
            else if constexpr(std::is_same_v<T, std::string>)
                property.type = PropertyType::String;
            else if constexpr(std::is_same_v<T, EntityUuid>)
                property.type = PropertyType::EntityReference;
            else if constexpr(std::is_same_v<T, ParameterMap>)
                property.type = PropertyType::Parameters;
            else {
                if constexpr(Sequence<T>::value) {
                    property.type = PropertyType::Sequence;
                    property.element = std::make_shared<PropertyDescriptor>(
                        describe_value<typename Sequence<T>::Element>(schema));
                    if constexpr(Sequence<T>::fixed)
                        property.fixed_size = Sequence<T>::size;
                } else if constexpr(Optional<T>::value) {
                    property.type = PropertyType::Optional;
                    property.element = std::make_shared<PropertyDescriptor>(
                        describe_value<typename Optional<T>::Element>(schema));
                } else {
                    property.type = PropertyType::Object;
                    if(schema)
                        property.members = schema->properties;
                }
                const auto members = property.members;
                const auto element = property.element;
                property.copy_callback = [members, element](
                                             const void* source) -> std::optional<PropertyValue> {
                    const auto& value = *static_cast<const T*>(source);
                    PropertyCollection::Data result;
                    if constexpr(Sequence<T>::value) {
                        result.kind = PropertyCollection::Kind::Sequence;
                        auto container = entt::forward_as_meta(context(), value);
                        auto view = container.as_sequence_container();
                        result.elements.reserve(view.size());
                        auto iterator = view.begin();
                        for(std::size_t index = 0; index < view.size(); ++index, ++iterator) {
                            std::optional<PropertyValue> copied;
                            if constexpr(std::is_same_v<typename Sequence<T>::Element, bool>) {
                                // 部分标准库的 const vector<bool> 也返回代理，不借用其地址。
                                const bool item = value[index];
                                copied = element->copy_value(&item);
                            } else {
                                const auto native = *iterator;
                                const auto* item =
                                    native.template try_cast<typename Sequence<T>::Element>();
                                if(!item)
                                    return std::nullopt;
                                copied = element->copy_value(item);
                            }
                            if(!copied)
                                return std::nullopt;
                            result.elements.push_back(std::move(*copied));
                        }
                    } else if constexpr(Optional<T>::value) {
                        result.kind = PropertyCollection::Kind::Optional;
                        if(value) {
                            auto copied = element->copy_value(&*value);
                            if(!copied)
                                return std::nullopt;
                            result.elements.push_back(std::move(*copied));
                        }
                    } else {
                        for(const auto& member : members) {
                            auto copied = member.copy_value(&value);
                            if(!copied)
                                return std::nullopt;
                            result.fields.emplace_back(member.id, std::move(*copied));
                        }
                    }
                    return PropertyCollection(std::move(result));
                };
                property.assign_callback = [members, element](void* destination,
                                               const PropertyValue& source,
                                               PropertyDescriptor::WriteMode mode) {
                    const auto* collection = std::get_if<PropertyCollection>(&source);
                    if(!collection)
                        return false;
                    const auto& data = collection->data();
                    T candidate = *static_cast<const T*>(destination);
                    if constexpr(Sequence<T>::value) {
                        auto container = entt::forward_as_meta(context(), candidate);
                        auto view = container.as_sequence_container();
                        if(view.size() != data.elements.size()
                            && !view.resize(data.elements.size()))
                            return false;
                        auto iterator = view.begin();
                        for(std::size_t index = 0; index < data.elements.size();
                            ++index, ++iterator) {
                            auto native = *iterator;
                            using Element = typename Sequence<T>::Element;
                            Element item;
                            if constexpr(std::is_same_v<Element, bool>) {
                                // vector<bool> 返回代理值；候选按实际 bool 写回，避免保留代理地址。
                                item = candidate[index];
                            } else {
                                const auto* source =
                                    std::as_const(native).template try_cast<Element>();
                                if(!source)
                                    return false;
                                item = *source;
                            }
                            if(element->copy_value(&item)
                                    != std::optional<PropertyValue>(data.elements[index])
                                && !element->assign_value(&item, data.elements[index], mode))
                                return false;
                            if constexpr(std::is_same_v<Element, bool>)
                                candidate[index] = item;
                            else if(!native.assign(entt::meta_any(context(), std::move(item))))
                                return false;
                        }
                    } else if constexpr(Optional<T>::value) {
                        if(data.elements.empty())
                            candidate.reset();
                        else {
                            if(!candidate)
                                candidate.emplace();
                            if(element->copy_value(&*candidate)
                                    != std::optional<PropertyValue>(data.elements.front())
                                && !element->assign_value(&*candidate, data.elements.front(), mode))
                                return false;
                        }
                    } else {
                        for(const auto& [name, value] : data.fields) {
                            const auto found =
                                std::ranges::find(members, name, &PropertyDescriptor::id);
                            if(found == members.end())
                                return false;
                            if(found->copy_value(&candidate) != std::optional<PropertyValue>(value)
                                && !found->assign_value(&candidate, value, mode))
                                return false;
                        }
                    }
                    *static_cast<T*>(destination) = std::move(candidate);
                    return true;
                };
            }
            // Captures a complete value schema, never a pointer into a movable registry.
            property.default_value = [shape = property] {
                const T value{};
                return shape.copy_value(&value).value();
            };
            return property;
        }
    }

    template<auto Member>
    PropertyDescriptor make_property_descriptor(
        std::string id, std::string display_name, PropertyMetadata metadata = {}) {
        using Value = ReflectionDetail::MemberValue<Member>;
        auto property = ReflectionDetail::describe_value<std::remove_cvref_t<Value>>();
        property.id = std::move(id);
        property.display_name = std::move(display_name);
        ReflectionDetail::apply_metadata(property, std::move(metadata));
        ReflectionDetail::bind_member<Member>(property);
        return property;
    }

    template<auto Member>
    PropertyDescriptor make_property_descriptor(std::string id, std::string display_name,
        const TypeDescriptor& schema, PropertyMetadata metadata = {}) {
        using Value = ReflectionDetail::MemberValue<Member>;
        auto property = ReflectionDetail::describe_value<std::remove_cvref_t<Value>>(&schema);
        property.id = std::move(id);
        property.display_name = std::move(display_name);
        ReflectionDetail::apply_metadata(property, std::move(metadata));
        ReflectionDetail::bind_member<Member>(property);
        return property;
    }

    template<auto Member, typename Callback>
    PropertyDescriptor make_property_descriptor(std::string id, std::string display_name,
        PropertyMetadata metadata, Callback&& on_changed) {
        using Value = ReflectionDetail::MemberValue<Member>;
        auto property = make_property_descriptor<Member>(
            std::move(id), std::move(display_name), std::move(metadata));
        property.on_changed = [callback = std::forward<Callback>(on_changed)](
                                  void* value) mutable { callback(*static_cast<Value*>(value)); };
        return property;
    }

    template<auto Member, std::size_t N>
        requires std::is_enum_v<ReflectionDetail::MemberValue<Member>>
    PropertyDescriptor make_enum_property_descriptor(std::string id, std::string display_name,
        const std::pair<ReflectionDetail::MemberValue<Member>, PropertyDescriptor::EnumOption> (
            &options)[N],
        PropertyMetadata metadata = {}) {
        using Enum = ReflectionDetail::MemberValue<Member>;
        PropertyDescriptor property{.id = std::move(id),
            .display_name = std::move(display_name),
            .type = PropertyType::Enum};
        ReflectionDetail::bind_member<Member>(property);
        ReflectionDetail::apply_metadata(property, std::move(metadata));
        for(const auto& [value, option] : options)
            property.enum_options.push_back(option);
        ReflectionDetail::bind_enumeration(options);
        property.read_enum = [](const void* value) {
            return ReflectionDetail::enum_name(entt::forward_as_meta(
                ReflectionDetail::context(), *static_cast<const Enum*>(value)));
        };
        property.write_enum = [](void* value, std::string_view id) {
            const auto candidate =
                ReflectionDetail::enum_value(entt::resolve<Enum>(ReflectionDetail::context()), id);
            const auto* converted = candidate.template try_cast<Enum>();
            if(!converted)
                return false;
            *static_cast<Enum*>(value) = *converted;
            return true;
        };
        property.default_value = [id = options[0].second.id] { return PropertyValue(id); };
        return property;
    }

    template<typename Object, typename Description = TypeDescriptor> class TypeBuilder {
    public:
        explicit TypeBuilder(Description description) : m_description(std::move(description)) {}
        template<auto Member>
        TypeBuilder& field(std::string id, ReflectedMember<Member> member,
            std::string display_name = {}, PropertyMetadata metadata = {}) {
            if(display_name.empty())
                display_name = id;
            m_description.properties.push_back(make_property_descriptor<Member>(
                std::move(id), std::move(display_name), std::move(metadata)));
            return *this;
        }
        template<auto Member>
        TypeBuilder& field(std::string id, ReflectedMember<Member> member, std::string display_name,
            const TypeDescriptor& schema, PropertyMetadata metadata = {}) {
            m_description.properties.push_back(make_property_descriptor<Member>(
                std::move(id), std::move(display_name), schema, std::move(metadata)));
            return *this;
        }
        template<auto Member, typename Callback>
        TypeBuilder& field(std::string id, ReflectedMember<Member> member, std::string display_name,
            PropertyMetadata metadata, Callback&& changed) {
            m_description.properties.push_back(make_property_descriptor<Member>(std::move(id),
                std::move(display_name), std::move(metadata), std::forward<Callback>(changed)));
            return *this;
        }
        template<auto Member, std::size_t N>
        TypeBuilder& enum_field(std::string id, ReflectedMember<Member> member,
            std::string display_name,
            const std::pair<ReflectionDetail::MemberValue<Member>, PropertyDescriptor::EnumOption> (
                &options)[N],
            PropertyMetadata metadata = {}) {
            m_description.properties.push_back(make_enum_property_descriptor<Member>(
                std::move(id), std::move(display_name), options, std::move(metadata)));
            return *this;
        }
        TypeBuilder& fields(std::vector<PropertyDescriptor> properties) {
            m_description.properties.insert(m_description.properties.end(),
                std::make_move_iterator(properties.begin()),
                std::make_move_iterator(properties.end()));
            return *this;
        }
        template<auto Method> TypeBuilder& method(std::string id) {
            const auto key = ReflectionDetail::identifier(id);
            if(!entt::resolve<Object>(ReflectionDetail::context()).func(key))
                entt::meta_factory<Object>{ReflectionDetail::context()}
                    .template func<Method>(key)
                    .template custom<ReflectionDetail::MethodMetadata>(
                        std::move(id), &entt::meta_invoke<Object, Method>);
            return *this;
        }
        [[nodiscard]] Description build() {
            ReflectionDetail::publish_type<Object>(m_description);
            return std::move(m_description);
        }

    private:
        Description m_description;
    };

    template<typename T>
    TypeBuilder<T> describe_type(std::string id, std::string display_name = {}) {
        if(display_name.empty())
            display_name = id;
        return TypeBuilder<T>(TypeDescriptor{std::move(id), std::move(display_name), {}});
    }
}

// 仅展开显式字段名和成员指针，不产生静态注册或隐藏初始化。
#define COMET_FIELD(Type, Member)                                                                  \
    #Member, ::Comet::ReflectedMember<&Type::Member> {}

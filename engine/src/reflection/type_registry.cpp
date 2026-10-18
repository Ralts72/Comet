#include "reflection/type_registry.h"
#include "reflection/entt_backend.h"

#include <exception>

namespace Comet::ReflectionDetail {
    entt::meta_ctx& context() {
        static entt::meta_ctx instance;
        return instance;
    }
    std::optional<std::string> enum_name(const entt::meta_any& value) {
        const auto* metadata = static_cast<const EnumMetadata*>(value.type().custom());
        if(metadata)
            for(const auto& [name, candidate] : metadata->values)
                if(value == candidate)
                    return name;
        return std::nullopt;
    }
    entt::meta_any enum_value(const entt::meta_type& type, std::string_view name) {
        const auto* metadata = static_cast<const EnumMetadata*>(type.custom());
        if(metadata)
            for(const auto& [id, candidate] : metadata->values)
                if(id == name)
                    return candidate;
        return {};
    }
}

namespace Comet::Reflection {
    namespace {
        entt::meta_type native_type(std::string_view id) {
            return entt::resolve(ReflectionDetail::context(), ReflectionDetail::identifier(id));
        }
        std::optional<PropertyValue> copy_result(const entt::meta_any& result) {
            std::optional<PropertyValue> value;
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                (
                    [&] {
                        using T = std::variant_alternative_t<I, PropertyValue>;
                        if(const auto* typed = result.try_cast<T>())
                            value = *typed;
                    }(),
                    ...);
            }(std::make_index_sequence<std::variant_size_v<PropertyValue>>{});
            if(result.type().is_enum())
                if(auto name = ReflectionDetail::enum_name(result))
                    value = std::move(*name);
            return value;
        }
    }

    struct Object::Data {
        entt::meta_any value;
        TypeDescriptor schema;
    };

    Object::Object(std::unique_ptr<Data> data) : m_data(std::move(data)) {}
    Object::~Object() = default;
    Object::Object(Object&&) noexcept = default;
    Object& Object::operator=(Object&&) noexcept = default;

    std::optional<PropertyValue> Object::copy_property(std::string_view path) const {
        if(!m_data)
            return std::nullopt;
        const auto property = m_data->schema.resolve_property(path);
        return property ? property->copy_value(m_data->value.base().data()) : std::nullopt;
    }
    bool Object::assign_property(std::string_view path, const PropertyValue& value) {
        if(!m_data)
            return false;
        const auto property = m_data->schema.resolve_property(path);
        if(!property)
            return false;
        auto candidate = m_data->value;
        // create() 只生成拥有独立内容的可复制对象，候选不会借用 const 组件。
        if(!property->assign_value(const_cast<void*>(candidate.base().data()), value))
            return false;
        m_data->value = std::move(candidate);
        return true;
    }
    Result<std::optional<PropertyValue>> Object::invoke(
        std::string_view name, std::span<const PropertyValue> arguments) {
        using Return = Result<std::optional<PropertyValue>>;
        if(!m_data)
            return Return::failure("反射对象无效");
        const auto method = m_data->value.type().func(ReflectionDetail::identifier(name));
        if(!method)
            return Return::failure("反射方法未注册: " + std::string(name));
        const auto* binding = static_cast<const ReflectionDetail::MethodMetadata*>(method.custom());
        if(!binding || method.arity() != arguments.size())
            return Return::failure("反射方法参数数量不匹配: " + std::string(name));
        std::vector<entt::meta_any> values;
        values.reserve(arguments.size());
        for(std::size_t index = 0; index < arguments.size(); ++index) {
            auto native = std::visit(
                [](const auto& value) {
                    return entt::meta_any(ReflectionDetail::context(), value);
                },
                arguments[index]);
            const auto expected = method.arg(index);
            if(expected.is_enum())
                if(const auto* text = std::get_if<std::string>(&arguments[index]))
                    native = ReflectionDetail::enum_value(expected, *text);
            if(!native || native.type() != expected)
                return Return::failure("反射方法参数类型不匹配: " + std::string(name));
            values.push_back(std::move(native));
        }
        try {
            const auto result = binding->invoke(m_data->value, values.data());
            if(!result)
                return Return::failure("反射方法调用失败: " + std::string(name));
            if(result.type().info() == entt::type_id<void>())
                return Return::success(std::nullopt);
            auto value = copy_result(result);
            if(!value)
                return Return::failure(
                    "反射方法返回类型不在 PropertyValue 范围内: " + std::string(name));
            return Return::success(std::move(value));
        } catch(const std::exception& error) {
            return Return::failure("反射方法调用失败: " + std::string(name) + ": " + error.what());
        }
    }

    std::optional<TypeDescriptor> find_type(std::string_view id) {
        const auto type = native_type(id);
        if(type)
            if(const auto* metadata =
                    static_cast<const ReflectionDetail::TypeMetadata*>(type.custom()))
                return metadata->schema;
        return std::nullopt;
    }
    std::vector<std::string> registered_types() {
        std::vector<std::string> result;
        for(const auto& [id, type] : entt::resolve(ReflectionDetail::context()))
            if(const auto* metadata =
                    static_cast<const ReflectionDetail::TypeMetadata*>(type.custom()))
                result.push_back(metadata->schema.id);
        std::ranges::sort(result);
        return result;
    }
    std::vector<std::string> methods(std::string_view id) {
        std::vector<std::string> result;
        if(const auto type = native_type(id))
            for(const auto& [key, method] : type.func())
                if(const auto* metadata =
                        static_cast<const ReflectionDetail::MethodMetadata*>(method.custom()))
                    result.push_back(metadata->id);
        return result;
    }
    Result<Object> create(std::string_view id) {
        const auto type = native_type(id);
        const auto* metadata =
            type ? static_cast<const ReflectionDetail::TypeMetadata*>(type.custom()) : nullptr;
        if(!metadata || !metadata->constructible)
            return Result<Object>::failure("反射类型未注册或不支持默认构造: " + std::string(id));
        auto value = type.construct();
        if(!value)
            return Result<Object>::failure("反射类型构造失败: " + std::string(id));
        return Result<Object>::success(Object(
            std::make_unique<Object::Data>(Object::Data{std::move(value), metadata->schema})));
    }
}

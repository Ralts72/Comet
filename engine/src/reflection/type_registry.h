#pragma once

#include "common/result.h"
#include "reflection/type.h"

namespace Comet::Reflection {
    // 动态构造的是独立值，不是 Scene 中的组件；实体修改仍走 ComponentDescriptor。
    class COMET_API Object {
    public:
        ~Object();
        Object(Object&&) noexcept;
        Object& operator=(Object&&) noexcept;
        Object(const Object&) = delete;
        Object& operator=(const Object&) = delete;
        [[nodiscard]] std::optional<PropertyValue> copy_property(std::string_view path) const;
        [[nodiscard]] bool assign_property(std::string_view path, const PropertyValue& value);
        [[nodiscard]] Result<std::optional<PropertyValue>> invoke(
            std::string_view method, std::span<const PropertyValue> arguments = {});

    private:
        struct Data;
        explicit Object(std::unique_ptr<Data> data);
        friend Result<Object> create(std::string_view id);
        std::unique_ptr<Data> m_data;
    };

    // 查询已显式注册的 Comet 类型，无第二份全局类型表或静态注册顺序。
    [[nodiscard]] COMET_API std::optional<TypeDescriptor> find_type(std::string_view id);
    [[nodiscard]] COMET_API std::vector<std::string> registered_types();
    [[nodiscard]] COMET_API std::vector<std::string> methods(std::string_view id);
    [[nodiscard]] COMET_API Result<Object> create(std::string_view id);
}

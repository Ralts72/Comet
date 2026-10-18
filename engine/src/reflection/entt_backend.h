#pragma once

#include "reflection/type.h"
#include <entt.hpp>

namespace Comet::ReflectionDetail {
    // 所有注册模板借用 engine 中的唯一上下文，避免 DLL 与宿主各自持有类型表。
    [[nodiscard]] COMET_API entt::meta_ctx& context();
    [[nodiscard]] inline entt::id_type identifier(std::string_view name) {
        return entt::hashed_string::value(name.data(), name.size());
    }
    struct TypeMetadata {
        TypeDescriptor schema;
        bool constructible = false;
    };
    struct EnumMetadata {
        std::vector<std::pair<std::string, entt::meta_any>> values;
    };
    struct MethodMetadata {
        std::string id;
        entt::meta_any (*invoke)(entt::meta_handle, entt::meta_any*) = nullptr;
    };
    [[nodiscard]] COMET_API std::optional<std::string> enum_name(const entt::meta_any& value);
    [[nodiscard]] COMET_API entt::meta_any enum_value(
        const entt::meta_type& type, std::string_view name);
}

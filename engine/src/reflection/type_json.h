#pragma once

#include "common/json.h"
#include "reflection/type.h"

namespace Comet::ReflectionJson {
    [[nodiscard]] COMET_API Result<PropertyValue> read_value(
        const PropertyDescriptor&, Json::Node, const Json::Context&, std::string_view location);
    [[nodiscard]] COMET_API Result<PropertyValue> copy_value(const PropertyDescriptor&,
        const void* object, const Json::Context&, std::string_view location);
    [[nodiscard]] COMET_API Result<void> write_value(Json::Writer&, const PropertyDescriptor&,
        const PropertyValue&, const Json::Context&, std::string_view location);

    // 先解析完整候选，失败不会留下部分写入。类型自身的跨字段校验由调用方负责。
    template<typename T>
    Result<void> read_object(const TypeDescriptor& schema, Json::Node node, T& destination,
        const Json::Context& context, std::string_view location) {
        PropertyDescriptor property{
            .id = schema.id, .type = PropertyType::Object, .members = schema.properties};
        auto value = read_value(property, node, context, location);
        if(!value)
            return Result<void>::failure(value.error());
        T candidate = destination;
        for(const auto& [name, field] : std::get<PropertyCollection>(value.value()).data().fields) {
            const auto* member = schema.find_property(name);
            if(!member
                || !member->assign_value(&candidate, field, PropertyDescriptor::WriteMode::Restore))
                return Result<void>::failure(
                    context.error(location, "cannot restore reflected object"));
        }
        destination = std::move(candidate);
        return Result<void>::success();
    }
    template<typename T>
    Result<void> write_object(Json::Writer& writer, const TypeDescriptor& schema, const T& value,
        const Json::Context& context, std::string_view location) {
        PropertyDescriptor property{
            .id = schema.id, .type = PropertyType::Object, .members = schema.properties};
        PropertyCollection::Data snapshot;
        for(const auto& member : schema.properties) {
            auto field =
                copy_value(member, &value, context, std::string(location) + "." + member.id);
            if(!field)
                return Result<void>::failure(field.error());
            snapshot.fields.emplace_back(member.id, std::move(field.value()));
        }
        return write_value(
            writer, property, PropertyCollection(std::move(snapshot)), context, location);
    }
}

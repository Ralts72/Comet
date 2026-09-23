#include "scene/scene_serializer.h"

#include "common/file_io.h"
#include "common/json.h"
#include "scene/component_registry.h"
#include "scene/components.h"
#include "scene/scene.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Comet {
    namespace {
        struct PropertyRecord {
            const PropertyDescriptor* descriptor = nullptr;
            PropertyValue value;
        };

        struct ComponentRecord {
            const ComponentDescriptor* descriptor = nullptr;
            std::vector<PropertyRecord> properties;
        };

        struct EntityRecord {
            EntityUuid uuid;
            std::optional<EntityUuid> parent;
            std::string name;
            std::vector<ComponentRecord> components;
        };

        Result<Math::Vec3> read_vec3(
            const Json::Node& node, const Json::Context& context, const std::string_view location) {
            const auto elements = context.array(node, location);
            if(!elements)
                return Result<Math::Vec3>::failure(elements.error());
            if(elements.value().size() != 3) {
                return Result<Math::Vec3>::failure(
                    context.error(location, "expected exactly three numbers"));
            }

            Math::Vec3 value;
            std::size_t index = 0;
            for(const auto element : elements.value()) {
                const auto scalar = context.read_scalar<float>(element,
                    std::string(location) + "[" + std::to_string(index) + "]", "a finite number");
                if(!scalar)
                    return Result<Math::Vec3>::failure(scalar.error());
                value[index] = scalar.value();
                ++index;
            }
            return Result<Math::Vec3>::success(value);
        }

        void write_vec3(Json::Writer& writer, const Math::Vec3& value) {
            writer.begin_array();
            writer.value(value.x);
            writer.value(value.y);
            writer.value(value.z);
            writer.end_array();
        }

        Result<PropertyValue> read_property_value(const PropertyDescriptor& property,
            const Json::Node& node, const Json::Context& context, const std::string_view location) {
            switch(property.type) {
                case PropertyType::Parameters: {
                    auto object = context.object(node, location);
                    if(!object)
                        return Result<PropertyValue>::failure(object.error());
                    ParameterMap parameters;
                    for(const auto field : object.value()) {
                        PropertyDescriptor item;
                        if(field.value.is_bool())
                            item.type = PropertyType::Bool;
                        else if(field.value.is_number())
                            item.type = PropertyType::Float;
                        else if(field.value.is_string())
                            item.type = PropertyType::String;
                        else if(field.value.is_array())
                            item.type = PropertyType::Vec3;
                        else
                            return Result<PropertyValue>::failure(
                                context.error(location, "Unsupported parameter type"));
                        auto value = read_property_value(item, field.value, context,
                            std::string(location) + "." + std::string(field.key));
                        if(!value)
                            return value;
                        const bool inserted = std::visit(
                            [&](const auto& scalar) {
                                using T = std::remove_cvref_t<decltype(scalar)>;
                                if constexpr(std::is_same_v<T, AssetHandle>
                                             || std::is_same_v<T, ParameterMap>)
                                    return false;
                                else
                                    return parameters.emplace(std::string(field.key), scalar)
                                        .second;
                            },
                            value.value());
                        if(!inserted || !valid_parameters(parameters))
                            return Result<PropertyValue>::failure(
                                context.error(location, "Invalid or duplicate parameter"));
                    }
                    return Result<PropertyValue>::success(std::move(parameters));
                }
                case PropertyType::Bool: {
                    auto value = context.read_scalar<bool>(node, location, "a boolean");
                    if(!value)
                        return Result<PropertyValue>::failure(value.error());
                    return Result<PropertyValue>::success(std::move(value).value());
                }
                case PropertyType::Enum:
                case PropertyType::String: {
                    auto value = context.read_scalar<std::string>(node, location, "a string");
                    if(!value)
                        return Result<PropertyValue>::failure(value.error());
                    if(property.type == PropertyType::Enum
                        && std::ranges::none_of(property.enum_options,
                            [&](const auto& option) { return option.id == value.value(); }))
                        return Result<PropertyValue>::failure(
                            context.error(location, "unknown enum name: " + value.value()));
                    return Result<PropertyValue>::success(std::move(value).value());
                }
                case PropertyType::Float: {
                    auto value = context.read_scalar<float>(node, location, "a finite number");
                    if(!value)
                        return Result<PropertyValue>::failure(value.error());
                    if(!property.accepts_value(value.value()))
                        return Result<PropertyValue>::failure(
                            context.error(location, "value outside allowed bounds"));
                    return Result<PropertyValue>::success(std::move(value).value());
                }
                case PropertyType::Vec3: {
                    auto value = read_vec3(node, context, location);
                    if(!value)
                        return Result<PropertyValue>::failure(value.error());
                    if(!property.accepts_value(value.value()))
                        return Result<PropertyValue>::failure(
                            context.error(location, "value outside allowed bounds"));
                    return Result<PropertyValue>::success(value.value());
                }
                case PropertyType::AssetHandle: {
                    auto value = context.read_scalar<AssetHandle::ValueType>(
                        node, location, "a non-negative integer");
                    if(!value)
                        return Result<PropertyValue>::failure(value.error());
                    return Result<PropertyValue>::success(AssetHandle(value.value()));
                }
            }
            return Result<PropertyValue>::failure(
                context.error(location, "unsupported property type"));
        }

        Result<PropertyValue> copy_property_value(const PropertyDescriptor& property,
            const void* component, const Json::Context& context, const std::string_view location) {
            auto value = property.copy_value(component);
            if(!value)
                return Result<PropertyValue>::failure(
                    context.error(location, "property accessor returned null or unsupported type"));
            if(const auto* number = std::get_if<float>(&*value); number && !std::isfinite(*number))
                return Result<PropertyValue>::failure(
                    context.error(location, "expected a finite number"));
            if(const auto* vector = std::get_if<Math::Vec3>(&*value);
                vector && !Math::is_finite(*vector))
                return Result<PropertyValue>::failure(
                    context.error(location, "expected finite numbers"));
            if(const auto* parameters = std::get_if<ParameterMap>(&*value);
                parameters && !valid_parameters(*parameters))
                return Result<PropertyValue>::failure(
                    context.error(location, "Invalid parameters"));
            if(!property.accepts_value(*value))
                return Result<PropertyValue>::failure(
                    context.error(location, "value violates property constraints"));
            return Result<PropertyValue>::success(std::move(*value));
        }

        void write_property_value(Json::Writer& writer, const PropertyRecord& property) {
            std::visit(
                [&writer](const auto& value) {
                    using T = std::remove_cvref_t<decltype(value)>;
                    if constexpr(std::is_same_v<T, Math::Vec3>)
                        write_vec3(writer, value);
                    else if constexpr(std::is_same_v<T, AssetHandle>)
                        writer.value(value.value());
                    else if constexpr(std::is_same_v<T, ParameterMap>) {
                        writer.begin_object();
                        for(const auto& [name, parameter] : value) {
                            writer.key(name);
                            std::visit(
                                [&](const auto& item) {
                                    if constexpr(std::is_same_v<std::remove_cvref_t<decltype(item)>,
                                                     Math::Vec3>)
                                        write_vec3(writer, item);
                                    else
                                        writer.value(item);
                                },
                                parameter);
                        }
                        writer.end_object();
                    } else
                        writer.value(value);
                },
                property.value);
        }

        std::string entity_location(const std::size_t index) {
            return "entities[" + std::to_string(index) + "]";
        }

        Result<EntityRecord> read_entity_record(const Json::Node& node, const std::string& location,
            const Json::Context& context, const ComponentRegistry& component_registry) {
            if(auto valid =
                    context.validate_keys(node, {"uuid", "components", "children"}, location);
                !valid)
                return Result<EntityRecord>::failure(valid.error());

            EntityRecord record;
            const auto uuid_text =
                context.read_field<std::string>(node, "uuid", "a UUID string", location);
            if(!uuid_text)
                return Result<EntityRecord>::failure(uuid_text.error());
            const auto uuid = EntityUuid::parse(uuid_text.value());
            if(!uuid || !*uuid)
                return Result<EntityRecord>::failure(
                    context.error(location + ".uuid", "expected a non-nil canonical UUID"));
            record.uuid = *uuid;

            const auto components_node = context.required_child(node, "components", location);
            if(!components_node)
                return Result<EntityRecord>::failure(components_node.error());
            const Json::Node components = components_node.value();
            std::vector<std::string_view> component_ids{"name"};
            component_ids.reserve(component_registry.components().size() + 1);
            for(const ComponentDescriptor& component : component_registry.components()) {
                if(component.serializable) {
                    component_ids.push_back(component.id);
                }
            }
            if(auto valid =
                    context.validate_keys(components, component_ids, location + ".components");
                !valid)
                return Result<EntityRecord>::failure(valid.error());

            auto name = context.read_field<std::string>(
                components, "name", "a string", location + ".components");
            if(!name)
                return Result<EntityRecord>::failure(name.error());
            record.name = std::move(name).value();

            for(const ComponentDescriptor& component_descriptor : component_registry.components()) {
                if(!component_descriptor.serializable) {
                    continue;
                }

                Json::Node component;
                if(components[component_descriptor.id].get(component) || component.is_null()) {
                    continue;
                }

                const std::string component_location =
                    location + ".components." + component_descriptor.id;
                std::vector<std::string_view> property_ids;
                property_ids.reserve(component_descriptor.properties.size());
                for(const PropertyDescriptor& property : component_descriptor.properties) {
                    if(property.serializable && !property.transient) {
                        property_ids.push_back(property.id);
                    }
                }
                if(auto valid = context.validate_keys(component, property_ids, component_location);
                    !valid)
                    return Result<EntityRecord>::failure(valid.error());

                ComponentRecord component_record{.descriptor = &component_descriptor};
                component_record.properties.reserve(property_ids.size());
                for(const PropertyDescriptor& property : component_descriptor.properties) {
                    if(!property.serializable || property.transient) {
                        continue;
                    }
                    const std::string property_location = component_location + "." + property.id;
                    if(!property.required
                        && component[property.id].error() == simdjson::NO_SUCH_FIELD)
                        continue;
                    const auto child =
                        context.required_child(component, property.id, component_location);
                    if(!child)
                        return Result<EntityRecord>::failure(child.error());
                    auto value =
                        read_property_value(property, child.value(), context, property_location);
                    if(!value)
                        return Result<EntityRecord>::failure(value.error());
                    component_record.properties.push_back(
                        {.descriptor = &property, .value = std::move(value).value()});
                }
                record.components.push_back(std::move(component_record));
            }

            return Result<EntityRecord>::success(std::move(record));
        }

        Result<void> read_entity_tree(const Json::Node node, const std::optional<EntityUuid> parent,
            const std::string& location, const Json::Context& context,
            const ComponentRegistry& component_registry, std::vector<EntityRecord>& records,
            std::unordered_set<EntityUuid>& uuids, const std::size_t depth) {
            if(depth > SceneSerializer::MAX_HIERARCHY_DEPTH)
                return Result<void>::failure(
                    context.error(location, "maximum hierarchy depth exceeded"));
            auto parsed = read_entity_record(node, location, context, component_registry);
            if(!parsed)
                return Result<void>::failure(parsed.error());
            auto record = std::move(parsed).value();
            const EntityUuid uuid = record.uuid;
            if(!uuids.insert(uuid).second)
                return Result<void>::failure(
                    context.error(location + ".uuid", "duplicate UUID " + uuid.to_string()));
            record.parent = parent;
            records.push_back(std::move(record));

            Json::Node children;
            if(!node["children"].get(children)) {
                const std::string children_location = location + ".children";
                const auto array = context.array(children, children_location);
                std::size_t index = 0;
                if(!array)
                    return Result<void>::failure(array.error());
                for(const auto child : array.value()) {
                    auto result = read_entity_tree(child, uuid,
                        children_location + "[" + std::to_string(index++) + "]", context,
                        component_registry, records, uuids, depth + 1);
                    if(!result)
                        return result;
                }
            }
            return Result<void>::success();
        }

        Result<void> validate_records(
            const std::vector<EntityRecord>& records, const Json::Context& context) {
            std::unordered_map<EntityUuid, std::size_t> indices;
            indices.reserve(records.size());
            for(std::size_t index = 0; index < records.size(); ++index) {
                if(!indices.emplace(records[index].uuid, index).second) {
                    return Result<void>::failure(context.error(entity_location(index) + ".uuid",
                        "duplicate UUID " + records[index].uuid.to_string()));
                }
            }

            for(std::size_t index = 0; index < records.size(); ++index) {
                if(records[index].parent && !indices.contains(*records[index].parent)) {
                    return Result<void>::failure(context.error(entity_location(index) + ".parent",
                        "missing parent UUID " + records[index].parent->to_string()));
                }
            }

            enum class VisitState { Visiting, Complete };
            std::unordered_map<EntityUuid, VisitState> states;
            states.reserve(records.size());
            const auto visit = [&records, &indices, &states, &context](const std::size_t index,
                                   const std::size_t depth, const auto& visit_ref) -> Result<void> {
                if(depth > SceneSerializer::MAX_HIERARCHY_DEPTH)
                    return Result<void>::failure(
                        context.error(entity_location(index), "maximum hierarchy depth exceeded"));
                const EntityUuid uuid = records[index].uuid;
                if(const auto state = states.find(uuid); state != states.end()) {
                    if(state->second == VisitState::Visiting) {
                        return Result<void>::failure(
                            context.error(entity_location(index) + ".parent",
                                "parent relationship forms a cycle"));
                    }
                    return Result<void>::success();
                }

                states.emplace(uuid, VisitState::Visiting);
                if(records[index].parent) {
                    if(auto result =
                            visit_ref(indices.at(*records[index].parent), depth + 1, visit_ref);
                        !result)
                        return result;
                }
                states[uuid] = VisitState::Complete;
                return Result<void>::success();
            };

            for(std::size_t index = 0; index < records.size(); ++index) {
                if(auto result = visit(index, 1, visit); !result)
                    return result;
            }
            return Result<void>::success();
        }

        using ChildrenIndex = std::unordered_map<EntityUuid, std::vector<std::size_t>>;

        Result<void> write_entity_tree(Json::Writer& writer, const EntityRecord& record,
            const std::vector<EntityRecord>& records, const ChildrenIndex& children,
            const std::size_t depth) {
            const Json::Context context("scene", "<memory>");
            if(depth > SceneSerializer::MAX_HIERARCHY_DEPTH)
                return Result<void>::failure(
                    context.error(record.uuid.to_string(), "maximum hierarchy depth exceeded"));
            writer.begin_object();
            writer.field("uuid", record.uuid.to_string());

            writer.key("components");
            writer.begin_object();
            writer.field("name", record.name);
            for(const ComponentRecord& component_record : record.components) {
                writer.key(component_record.descriptor->id);
                writer.begin_object();
                for(const PropertyRecord& property : component_record.properties) {
                    writer.key(property.descriptor->id);
                    write_property_value(writer, property);
                }
                writer.end_object();
            }
            writer.end_object();
            if(const auto found = children.find(record.uuid); found != children.end()) {
                writer.key("children");
                writer.begin_array();
                for(const auto index : found->second) {
                    if(auto result =
                            write_entity_tree(writer, records[index], records, children, depth + 1);
                        !result)
                        return result;
                }
                writer.end_array();
            }
            writer.end_object();
            return Result<void>::success();
        }
    }

    SceneSerializer::SceneSerializer(const ComponentRegistry& component_registry)
        : m_component_registry(component_registry) {}

    Result<std::string> SceneSerializer::serialize(const Scene& scene) const {
        const Json::Context context("scene", "<memory>");
        std::vector<EntityRecord> records;
        records.reserve(scene.entity_count());

        std::unordered_map<EntityId, EntityUuid> uuids_by_id;
        uuids_by_id.reserve(scene.entity_count());
        std::unordered_map<EntityUuid, entt::entity> handles_by_uuid;
        handles_by_uuid.reserve(scene.entity_count());
        const auto entities = scene.m_registry.view<IdComponent>();
        for(const entt::entity handle : entities) {
            const auto* uuid = scene.m_registry.try_get<UuidComponent>(handle);
            const auto* name = scene.m_registry.try_get<NameComponent>(handle);
            const EntityId id = entities.get<IdComponent>(handle).id;
            if(!uuid || !uuid->uuid) {
                return Result<std::string>::failure(
                    context.error("entities", "entity has no valid UUID"));
            }
            if(!name) {
                return Result<std::string>::failure(
                    context.error(uuid->uuid.to_string(), "entity has no NameComponent"));
            }
            if(!uuids_by_id.emplace(id, uuid->uuid).second) {
                return Result<std::string>::failure(context.error(
                    uuid->uuid.to_string(), "duplicate runtime EntityId " + std::to_string(id)));
            }
            if(!handles_by_uuid.emplace(uuid->uuid, handle).second) {
                return Result<std::string>::failure(
                    context.error(uuid->uuid.to_string(), "duplicate UUID"));
            }

            EntityRecord record{.uuid = uuid->uuid, .name = name->name};
            const Entity entity(handle, const_cast<Scene*>(&scene));
            for(const ComponentDescriptor& component_descriptor :
                m_component_registry.components()) {
                if(!component_descriptor.serializable
                    || !component_descriptor.has_component(entity)) {
                    continue;
                }

                const void* component = component_descriptor.get_component(entity);
                const std::string component_location =
                    uuid->uuid.to_string() + ".components." + component_descriptor.id;
                if(component == nullptr) {
                    return Result<std::string>::failure(
                        context.error(component_location, "component accessor returned null"));
                }

                ComponentRecord component_record{.descriptor = &component_descriptor};
                component_record.properties.reserve(component_descriptor.properties.size());
                for(const PropertyDescriptor& property : component_descriptor.properties) {
                    if(!property.serializable || property.transient) {
                        continue;
                    }
                    auto value = copy_property_value(
                        property, component, context, component_location + "." + property.id);
                    if(!value)
                        return Result<std::string>::failure(value.error());
                    component_record.properties.push_back(
                        {.descriptor = &property, .value = std::move(value).value()});
                }
                record.components.push_back(std::move(component_record));
            }
            records.push_back(std::move(record));
        }

        for(std::size_t index = 0; index < records.size(); ++index) {
            const auto* relationship = scene.m_registry.try_get<RelationshipComponent>(
                handles_by_uuid.at(records[index].uuid));
            if(!relationship || relationship->parent == INVALID_ENTITY_ID) {
                continue;
            }
            const auto parent = uuids_by_id.find(relationship->parent);
            if(parent == uuids_by_id.end()) {
                return Result<std::string>::failure(context.error(records[index].uuid.to_string(),
                    "relationship references missing runtime parent "
                        + std::to_string(relationship->parent)));
            }
            records[index].parent = parent->second;
        }

        std::ranges::sort(records, {}, &EntityRecord::uuid);
        if(auto valid = validate_records(records, context); !valid)
            return Result<std::string>::failure(valid.error());
        ChildrenIndex children;
        for(std::size_t index = 0; index < records.size(); ++index)
            children[records[index].parent.value_or(INVALID_ENTITY_UUID)].push_back(index);

        Json::Writer writer;
        writer.begin_object();
        writer.field("version", std::uint64_t(FORMAT_VERSION));
        const auto& environment = scene.get_environment();
        writer.key("environment");
        writer.begin_object();
        writer.field("asset", environment.asset.value());
        writer.field("background", environment.background);
        writer.field("intensity", environment.intensity);
        writer.field("rotation", environment.rotation);
        writer.field("lighting", environment.lighting);
        writer.field("lighting_intensity", environment.lighting_intensity);
        writer.key("background_color");
        write_vec3(writer, environment.background_color);
        writer.end_object();
        const auto& post_process = scene.get_post_process();
        writer.key("post_process");
        writer.begin_object();
        writer.field("exposure", post_process.exposure);
        writer.field("bloom_enabled", post_process.bloom_enabled);
        writer.field("bloom_strength", post_process.bloom_strength);
        writer.field("bloom_threshold", post_process.bloom_threshold);
        writer.end_object();
        writer.key("entities");
        writer.begin_array();
        for(const auto index : children[INVALID_ENTITY_UUID]) {
            if(auto result = write_entity_tree(writer, records[index], records, children, 1);
                !result)
                return Result<std::string>::failure(result.error());
        }
        writer.end_array();
        writer.end_object();
        return std::move(writer).finish();
    }

    Result<std::unique_ptr<Scene>> SceneSerializer::deserialize(
        const std::string_view contents, const std::string_view source) const {
        using LoadResult = Result<std::unique_ptr<Scene>>;
        simdjson::dom::parser parser;
        const Json::Context context("scene", source);
        const auto parsed = context.parse(parser, contents);
        if(!parsed)
            return LoadResult::failure(parsed.error());
        const Json::Node root = parsed.value();

        if(auto valid = context.validate_keys(
               root, {"version", "entities", "environment", "post_process"}, "<root>");
            !valid)
            return LoadResult::failure(valid.error());
        const auto version =
            context.read_field<std::uint32_t>(root, "version", "a non-negative integer");
        if(!version)
            return LoadResult::failure(version.error());
        if(version.value() != FORMAT_VERSION) {
            return LoadResult::failure(
                context.error("version", "unsupported version " + std::to_string(version.value())
                                             + "; expected " + std::to_string(FORMAT_VERSION)));
        }

        const auto entities_node = context.required_child(root, "entities", "<root>");
        if(!entities_node)
            return LoadResult::failure(entities_node.error());
        const auto entities = context.array(entities_node.value(), "entities");
        if(!entities)
            return LoadResult::failure(entities.error());
        std::vector<EntityRecord> records;
        records.reserve(entities.value().size());
        std::unordered_set<EntityUuid> uuids;
        std::size_t index = 0;
        for(const auto entity : entities.value()) {
            auto result = read_entity_tree(entity, std::nullopt, entity_location(index++), context,
                m_component_registry, records, uuids, 1);
            if(!result)
                return LoadResult::failure(result.error());
        }

        auto scene = std::make_unique<Scene>();
        Json::Node environment_node;
        if(const auto error = root["environment"].get(environment_node);
            error != simdjson::NO_SUCH_FIELD) {
            if(error)
                return LoadResult::failure(context.error("environment", "invalid object"));
            if(auto valid = context.validate_keys(environment_node,
                   {"asset", "background", "intensity", "rotation", "lighting",
                       "lighting_intensity", "background_color"},
                   "environment");
                !valid)
                return LoadResult::failure(valid.error());
            auto asset = context.read_field<uint64_t>(
                environment_node, "asset", "an asset handle", "environment");
            auto background = context.read_field<bool>(
                environment_node, "background", "a boolean", "environment");
            auto intensity = context.read_field<float>(
                environment_node, "intensity", "a finite number", "environment");
            auto rotation = context.read_field<float>(
                environment_node, "rotation", "a finite number", "environment");
            if(!asset)
                return LoadResult::failure(asset.error());
            if(!background)
                return LoadResult::failure(background.error());
            if(!intensity)
                return LoadResult::failure(intensity.error());
            if(!rotation)
                return LoadResult::failure(rotation.error());
            SceneEnvironment environment{AssetHandle(asset.value()), background.value(),
                intensity.value(), rotation.value()};
            if(environment_node["lighting"].error() != simdjson::NO_SUCH_FIELD) {
                auto lighting = context.read_field<bool>(
                    environment_node, "lighting", "a boolean", "environment");
                if(!lighting)
                    return LoadResult::failure(lighting.error());
                environment.lighting = lighting.value();
            }
            if(environment_node["lighting_intensity"].error() != simdjson::NO_SUCH_FIELD) {
                auto lighting_intensity = context.read_field<float>(
                    environment_node, "lighting_intensity", "a finite number", "environment");
                if(!lighting_intensity)
                    return LoadResult::failure(lighting_intensity.error());
                environment.lighting_intensity = lighting_intensity.value();
            }
            Json::Node color_node;
            if(const auto error = environment_node["background_color"].get(color_node);
                error != simdjson::NO_SUCH_FIELD) {
                if(error)
                    return LoadResult::failure(
                        context.error("environment.background_color", "invalid color"));
                auto color = read_vec3(color_node, context, "environment.background_color");
                if(!color)
                    return LoadResult::failure(color.error());
                environment.background_color = color.value();
            }
            if(auto valid = environment.validate(); !valid)
                return LoadResult::failure(context.error("environment", valid.error()));
            static_cast<void>(scene->set_environment(environment));
        }
        Json::Node post_process_node;
        if(const auto error = root["post_process"].get(post_process_node);
            error != simdjson::NO_SUCH_FIELD) {
            if(error)
                return LoadResult::failure(context.error("post_process", "invalid object"));
            if(auto valid = context.validate_keys(post_process_node,
                   {"exposure", "bloom_enabled", "bloom_strength", "bloom_threshold"},
                   "post_process");
                !valid)
                return LoadResult::failure(valid.error());
            const auto exposure = context.read_field<float>(
                post_process_node, "exposure", "a finite number", "post_process");
            const auto enabled = context.read_field<bool>(
                post_process_node, "bloom_enabled", "a boolean", "post_process");
            const auto strength = context.read_field<float>(
                post_process_node, "bloom_strength", "a finite number", "post_process");
            const auto threshold = context.read_field<float>(
                post_process_node, "bloom_threshold", "a finite number", "post_process");
            if(!exposure)
                return LoadResult::failure(exposure.error());
            if(!enabled)
                return LoadResult::failure(enabled.error());
            if(!strength)
                return LoadResult::failure(strength.error());
            if(!threshold)
                return LoadResult::failure(threshold.error());
            const PostProcessSettings settings{
                exposure.value(), enabled.value(), strength.value(), threshold.value()};
            if(auto valid = settings.validate(); !valid)
                return LoadResult::failure(context.error("post_process", valid.error()));
            scene->m_post_process = settings;
        }
        std::unordered_map<EntityUuid, Entity> loaded_entities;
        loaded_entities.reserve(records.size());
        for(const EntityRecord& record : records) {
            Entity entity = scene->create_entity_with_uuid(record.uuid, record.name);
            if(!entity) {
                return LoadResult::failure(
                    context.error(record.uuid.to_string(), "failed to create entity"));
            }
            entity.get_component<NameComponent>().name = record.name;
            for(const ComponentDescriptor& component_descriptor :
                m_component_registry.components()) {
                if(!component_descriptor.serializable) {
                    continue;
                }

                const auto component_record = std::ranges::find_if(
                    record.components, [&component_descriptor](const ComponentRecord& component) {
                        return component.descriptor == &component_descriptor;
                    });
                const std::string component_location =
                    record.uuid.to_string() + ".components." + component_descriptor.id;
                if(component_record == record.components.end()) {
                    if(component_descriptor.has_component(entity)
                        && !component_descriptor.remove_component(entity)) {
                        return LoadResult::failure(
                            context.error(component_location, "failed to remove absent component"));
                    }
                    continue;
                }

                if(!component_descriptor.has_component(entity)
                    && !component_descriptor.add_component(entity)) {
                    return LoadResult::failure(
                        context.error(component_location, "failed to create component"));
                }
                for(const PropertyRecord& property : component_record->properties) {
                    if(!component_descriptor.assign_property(entity, property.descriptor->id,
                           property.value, PropertyDescriptor::WriteMode::Restore))
                        return LoadResult::failure(
                            context.error(component_location + "." + property.descriptor->id,
                                "failed to restore property"));
                }
            }
            loaded_entities.emplace(record.uuid, entity);
        }

        for(const EntityRecord& record : records) {
            if(!record.parent) {
                continue;
            }
            if(!scene->set_parent(
                   loaded_entities.at(record.uuid), loaded_entities.at(*record.parent))) {
                return LoadResult::failure(context.error(
                    record.uuid.to_string(), "failed to restore parent relationship"));
            }
        }
        scene->update_world_transforms();
        return Result<std::unique_ptr<Scene>>::success(std::move(scene));
    }

    Result<std::unique_ptr<Scene>> SceneSerializer::clone(const Scene& scene) const {
        auto contents = serialize(scene);
        if(!contents)
            return Result<std::unique_ptr<Scene>>::failure(contents.error());
        return deserialize(contents.value(), "<scene-clone>");
    }

    Result<void> SceneSerializer::save(const Scene& scene, const std::string& path) const {
        const std::filesystem::path scene_path(path);
        if(scene_path.empty()) {
            return Result<void>::failure("Scene path cannot be empty");
        }
        auto contents = serialize(scene);
        if(!contents)
            return Result<void>::failure(contents.error());
        return write_text_file_atomic(scene_path, contents.value());
    }

    Result<std::unique_ptr<Scene>> SceneSerializer::load(const std::string& path) const {
        auto contents = read_text_file(path);
        if(!contents)
            return Result<std::unique_ptr<Scene>>::failure(contents.error());
        return deserialize(contents.value(), path);
    }
}

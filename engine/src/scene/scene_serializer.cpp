#include "scene/scene_serializer.h"

#include "common/file_io.h"
#include "common/yaml_utils.h"
#include "scene/component_registry.h"
#include "scene/components.h"
#include "scene/scene.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace Comet {
    namespace {
        using PropertyValue = std::variant<bool, float, Math::Vec3, AssetHandle>;

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

        std::runtime_error scene_error(const std::string_view source,
            const std::string_view location, const std::string& detail) {
            return std::runtime_error("Invalid scene '" + std::string(source) + "' at '"
                                      + std::string(location) + "': " + detail);
        }

        void require_sequence(const YAML::Node& node, const std::string_view source,
            const std::string_view location) {
            Yaml::require_sequence(node, source, location, scene_error);
        }

        template<typename AllowedKeys>
        void validate_keys_in(const YAML::Node& node, const AllowedKeys& allowed,
            const std::string_view source, const std::string_view location) {
            Yaml::validate_keys(node, allowed, source, location, scene_error);
        }

        void validate_keys(const YAML::Node& node,
            const std::initializer_list<std::string_view> allowed,
            const std::string_view source, const std::string_view location) {
            validate_keys_in(node, allowed, source, location);
        }

        void validate_keys(const YAML::Node& node,
            const std::vector<std::string_view>& allowed, const std::string_view source,
            const std::string_view location) {
            validate_keys_in(node, allowed, source, location);
        }

        YAML::Node required_child(const YAML::Node& node, const std::string_view key,
            const std::string_view source, const std::string_view location) {
            return Yaml::required_child(node, key, source, location, scene_error);
        }

        template<typename T>
        T read_scalar(const YAML::Node& node, const std::string_view source,
            const std::string_view location, const std::string_view expected) {
            return Yaml::read_scalar<T>(node, source, location, expected, scene_error);
        }

        template<typename T>
        T read_unsigned_integer(const YAML::Node& node, const std::string_view source,
            const std::string_view location) {
            if(!node.IsDefined() || !node.IsScalar()) {
                throw scene_error(source, location, "expected a non-negative integer");
            }

            const std::string text = node.Scalar();
            T value{};
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), value);
            if(error != std::errc{} || end != text.data() + text.size()) {
                throw scene_error(source, location, "expected a non-negative integer");
            }
            return value;
        }

        EntityUuid read_uuid(const YAML::Node& node, const std::string_view source,
            const std::string_view location) {
            const std::string value =
                read_scalar<std::string>(node, source, location, "a UUID string");
            const auto uuid = EntityUuid::parse(value);
            if(!uuid || !*uuid) {
                throw scene_error(source, location, "expected a non-nil canonical UUID");
            }
            return *uuid;
        }

        Math::Vec3 read_vec3(const YAML::Node& node, const std::string_view source,
            const std::string_view location) {
            require_sequence(node, source, location);
            if(node.size() != 3) {
                throw scene_error(source, location, "expected exactly three numbers");
            }

            Math::Vec3 value;
            for(std::size_t index = 0; index < 3; ++index) {
                value[index] = read_scalar<float>(node[index], source,
                    std::string(location) + "[" + std::to_string(index) + "]",
                    "a finite number");
                if(!std::isfinite(value[index])) {
                    throw scene_error(source, location, "expected finite numbers");
                }
            }
            return value;
        }

        YAML::Node write_vec3(const Math::Vec3& value) {
            YAML::Node node(YAML::NodeType::Sequence);
            node.SetStyle(YAML::EmitterStyle::Flow);
            node.push_back(value.x);
            node.push_back(value.y);
            node.push_back(value.z);
            return node;
        }

        bool is_finite(const Math::Vec3& value) {
            return std::isfinite(value.x) && std::isfinite(value.y)
                   && std::isfinite(value.z);
        }

        PropertyValue read_property_value(const PropertyDescriptor& property,
            const YAML::Node& node, const std::string_view source,
            const std::string_view location) {
            switch(property.type) {
                case PropertyType::Bool:
                    return read_scalar<bool>(node, source, location, "a boolean");
                case PropertyType::Float: {
                    const float value =
                        read_scalar<float>(node, source, location, "a finite number");
                    if(!std::isfinite(value)) {
                        throw scene_error(source, location, "expected a finite number");
                    }
                    return value;
                }
                case PropertyType::Vec3:
                    return read_vec3(node, source, location);
                case PropertyType::AssetHandle:
                    return AssetHandle(read_unsigned_integer<AssetHandle::ValueType>(
                        node, source, location));
            }
            throw scene_error(source, location, "unsupported property type");
        }

        PropertyValue copy_property_value(const PropertyDescriptor& property,
            const void* component, const std::string_view location) {
            const void* value = property.get_value(component);
            if(value == nullptr) {
                throw scene_error(
                    "<memory>", location, "property accessor returned null");
            }

            switch(property.type) {
                case PropertyType::Bool:
                    return *static_cast<const bool*>(value);
                case PropertyType::Float: {
                    const float result = *static_cast<const float*>(value);
                    if(!std::isfinite(result)) {
                        throw scene_error(
                            "<memory>", location, "expected a finite number");
                    }
                    return result;
                }
                case PropertyType::Vec3: {
                    const Math::Vec3 result = *static_cast<const Math::Vec3*>(value);
                    if(!is_finite(result)) {
                        throw scene_error(
                            "<memory>", location, "expected finite numbers");
                    }
                    return result;
                }
                case PropertyType::AssetHandle:
                    return *static_cast<const AssetHandle*>(value);
            }
            throw scene_error("<memory>", location, "unsupported property type");
        }

        YAML::Node write_property_value(const PropertyRecord& property) {
            switch(property.descriptor->type) {
                case PropertyType::Bool:
                    return YAML::Node(std::get<bool>(property.value));
                case PropertyType::Float:
                    return YAML::Node(std::get<float>(property.value));
                case PropertyType::Vec3:
                    return write_vec3(std::get<Math::Vec3>(property.value));
                case PropertyType::AssetHandle:
                    return YAML::Node(std::get<AssetHandle>(property.value).value());
            }
            return {};
        }

        void assign_property_value(const PropertyRecord& property, void* component,
            const std::string_view source, const std::string_view location) {
            void* value = property.descriptor->get_value(component);
            if(value == nullptr) {
                throw scene_error(source, location, "property accessor returned null");
            }

            switch(property.descriptor->type) {
                case PropertyType::Bool:
                    *static_cast<bool*>(value) = std::get<bool>(property.value);
                    return;
                case PropertyType::Float:
                    *static_cast<float*>(value) = std::get<float>(property.value);
                    return;
                case PropertyType::Vec3:
                    *static_cast<Math::Vec3*>(value) =
                        std::get<Math::Vec3>(property.value);
                    return;
                case PropertyType::AssetHandle:
                    *static_cast<AssetHandle*>(value) =
                        std::get<AssetHandle>(property.value);
                    return;
            }
            throw scene_error(source, location, "unsupported property type");
        }

        std::string entity_location(const std::size_t index) {
            return "entities[" + std::to_string(index) + "]";
        }

        EntityRecord read_entity_record(const YAML::Node& node, const std::size_t index,
            const std::string_view source, const ComponentRegistry& component_registry) {
            const std::string location = entity_location(index);
            validate_keys(node, {"uuid", "parent", "components"}, source, location);

            EntityRecord record;
            record.uuid = read_uuid(required_child(node, "uuid", source, location),
                source, location + ".uuid");

            const YAML::Node parent = node["parent"];
            if(parent.IsDefined() && !parent.IsNull()) {
                record.parent = read_uuid(parent, source, location + ".parent");
            }

            const YAML::Node components =
                required_child(node, "components", source, location);
            std::vector<std::string_view> component_ids{"name"};
            component_ids.reserve(component_registry.components().size() + 1);
            for(const ComponentDescriptor& component : component_registry.components()) {
                if(component.serializable) {
                    component_ids.push_back(component.id);
                }
            }
            validate_keys(components, component_ids, source, location + ".components");

            record.name = read_scalar<std::string>(
                required_child(components, "name", source, location + ".components"),
                source, location + ".components.name", "a string");

            for(const ComponentDescriptor& component_descriptor :
                component_registry.components()) {
                if(!component_descriptor.serializable) {
                    continue;
                }

                const YAML::Node component = components[component_descriptor.id];
                if(!component.IsDefined() || component.IsNull()) {
                    continue;
                }

                const std::string component_location =
                    location + ".components." + component_descriptor.id;
                std::vector<std::string_view> property_ids;
                property_ids.reserve(component_descriptor.properties.size());
                for(const PropertyDescriptor& property :
                    component_descriptor.properties) {
                    if(property.serializable && !property.transient) {
                        property_ids.push_back(property.id);
                    }
                }
                validate_keys(component, property_ids, source, component_location);

                ComponentRecord component_record{.descriptor = &component_descriptor};
                component_record.properties.reserve(property_ids.size());
                for(const PropertyDescriptor& property :
                    component_descriptor.properties) {
                    if(!property.serializable || property.transient) {
                        continue;
                    }
                    const std::string property_location =
                        component_location + "." + property.id;
                    component_record.properties.push_back({.descriptor = &property,
                        .value = read_property_value(property,
                            required_child(
                                component, property.id, source, component_location),
                            source, property_location)});
                }
                record.components.push_back(std::move(component_record));
            }

            return record;
        }

        void validate_records(
            const std::vector<EntityRecord>& records, const std::string_view source) {
            std::unordered_map<EntityUuid, std::size_t> indices;
            indices.reserve(records.size());
            for(std::size_t index = 0; index < records.size(); ++index) {
                if(!indices.emplace(records[index].uuid, index).second) {
                    throw scene_error(source, entity_location(index) + ".uuid",
                        "duplicate UUID " + records[index].uuid.to_string());
                }
            }

            for(std::size_t index = 0; index < records.size(); ++index) {
                if(records[index].parent && !indices.contains(*records[index].parent)) {
                    throw scene_error(source, entity_location(index) + ".parent",
                        "missing parent UUID " + records[index].parent->to_string());
                }
            }

            enum class VisitState { Visiting, Complete };
            std::unordered_map<EntityUuid, VisitState> states;
            states.reserve(records.size());
            const auto visit = [&records, &indices, &states, source](
                                   const std::size_t index,
                                   const auto& visit_ref) -> void {
                const EntityUuid uuid = records[index].uuid;
                if(const auto state = states.find(uuid); state != states.end()) {
                    if(state->second == VisitState::Visiting) {
                        throw scene_error(source, entity_location(index) + ".parent",
                            "parent relationship forms a cycle");
                    }
                    return;
                }

                states.emplace(uuid, VisitState::Visiting);
                if(records[index].parent) {
                    visit_ref(indices.at(*records[index].parent), visit_ref);
                }
                states[uuid] = VisitState::Complete;
            };

            for(std::size_t index = 0; index < records.size(); ++index) {
                visit(index, visit);
            }
        }

        YAML::Node write_entity_record(const EntityRecord& record) {
            YAML::Node entity(YAML::NodeType::Map);
            entity["uuid"] = record.uuid.to_string();
            if(record.parent) {
                entity["parent"] = record.parent->to_string();
            }

            YAML::Node components(YAML::NodeType::Map);
            components["name"] = record.name;
            for(const ComponentRecord& component_record : record.components) {
                YAML::Node component(YAML::NodeType::Map);
                for(const PropertyRecord& property : component_record.properties) {
                    component[property.descriptor->id] = write_property_value(property);
                }
                components[component_record.descriptor->id] = component;
            }
            entity["components"] = components;
            return entity;
        }
    }

    SceneSerializer::SceneSerializer(const ComponentRegistry& component_registry)
        : m_component_registry(component_registry) {}

    std::string SceneSerializer::serialize(const Scene& scene) const {
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
                throw scene_error("<memory>", "entities", "entity has no valid UUID");
            }
            if(!name) {
                throw scene_error(
                    "<memory>", uuid->uuid.to_string(), "entity has no NameComponent");
            }
            if(!uuids_by_id.emplace(id, uuid->uuid).second) {
                throw scene_error("<memory>", uuid->uuid.to_string(),
                    "duplicate runtime EntityId " + std::to_string(id));
            }
            if(!handles_by_uuid.emplace(uuid->uuid, handle).second) {
                throw scene_error("<memory>", uuid->uuid.to_string(), "duplicate UUID");
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
                    throw scene_error("<memory>", component_location,
                        "component accessor returned null");
                }

                ComponentRecord component_record{.descriptor = &component_descriptor};
                component_record.properties.reserve(
                    component_descriptor.properties.size());
                for(const PropertyDescriptor& property :
                    component_descriptor.properties) {
                    if(!property.serializable || property.transient) {
                        continue;
                    }
                    component_record.properties.push_back({.descriptor = &property,
                        .value = copy_property_value(property, component,
                            component_location + "." + property.id)});
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
                throw scene_error("<memory>", records[index].uuid.to_string(),
                    "relationship references missing runtime parent "
                        + std::to_string(relationship->parent));
            }
            records[index].parent = parent->second;
        }

        std::ranges::sort(records, {}, &EntityRecord::uuid);
        validate_records(records, "<memory>");

        YAML::Node root(YAML::NodeType::Map);
        root["version"] = FORMAT_VERSION;
        YAML::Node entities_node(YAML::NodeType::Sequence);
        for(const EntityRecord& record : records) {
            entities_node.push_back(write_entity_record(record));
        }
        root["entities"] = entities_node;

        YAML::Emitter emitter;
        emitter << root;
        if(!emitter.good()) {
            throw std::runtime_error(
                "Failed to serialize scene: " + emitter.GetLastError());
        }
        return std::string(emitter.c_str()) + '\n';
    }

    std::unique_ptr<Scene> SceneSerializer::deserialize(
        const std::string_view contents, const std::string_view source) const {
        YAML::Node root;
        try {
            root = YAML::Load(std::string(contents));
        } catch(const YAML::Exception& error) {
            throw std::runtime_error(
                "Failed to parse scene '" + std::string(source) + "': " + error.what());
        }

        validate_keys(root, {"version", "entities"}, source, "<root>");
        const std::uint32_t version = read_unsigned_integer<std::uint32_t>(
            required_child(root, "version", source, "<root>"), source, "version");
        if(version != FORMAT_VERSION) {
            throw scene_error(source, "version",
                "unsupported version " + std::to_string(version) + "; expected "
                    + std::to_string(FORMAT_VERSION));
        }

        const YAML::Node entities = required_child(root, "entities", source, "<root>");
        require_sequence(entities, source, "entities");
        std::vector<EntityRecord> records;
        records.reserve(entities.size());
        for(std::size_t index = 0; index < entities.size(); ++index) {
            records.push_back(
                read_entity_record(entities[index], index, source, m_component_registry));
        }
        validate_records(records, source);

        auto scene = std::make_unique<Scene>();
        std::unordered_map<EntityUuid, Entity> loaded_entities;
        loaded_entities.reserve(records.size());
        for(const EntityRecord& record : records) {
            Entity entity = scene->create_entity_with_uuid(record.uuid, record.name);
            if(!entity) {
                throw scene_error(
                    source, record.uuid.to_string(), "failed to create entity");
            }
            entity.get_component<NameComponent>().name = record.name;
            for(const ComponentDescriptor& component_descriptor :
                m_component_registry.components()) {
                if(!component_descriptor.serializable) {
                    continue;
                }

                const auto component_record = std::ranges::find_if(record.components,
                    [&component_descriptor](const ComponentRecord& component) {
                        return component.descriptor == &component_descriptor;
                    });
                const std::string component_location =
                    record.uuid.to_string() + ".components." + component_descriptor.id;
                if(component_record == record.components.end()) {
                    if(component_descriptor.has_component(entity)
                        && !component_descriptor.remove_component(entity)) {
                        throw scene_error(source, component_location,
                            "failed to remove absent component");
                    }
                    continue;
                }

                if(!component_descriptor.has_component(entity)
                    && !component_descriptor.add_component(entity)) {
                    throw scene_error(
                        source, component_location, "failed to create component");
                }
                void* component = component_descriptor.get_component(entity);
                if(component == nullptr) {
                    throw scene_error(
                        source, component_location, "component accessor returned null");
                }
                for(const PropertyRecord& property : component_record->properties) {
                    assign_property_value(property, component, source,
                        component_location + "." + property.descriptor->id);
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
                throw scene_error(source, record.uuid.to_string(),
                    "failed to restore parent relationship");
            }
        }
        scene->update_world_transforms();
        return scene;
    }

    std::unique_ptr<Scene> SceneSerializer::clone(const Scene& scene) const {
        return deserialize(serialize(scene), "<scene-clone>");
    }

    void SceneSerializer::save(const Scene& scene, const std::string& path) const {
        const std::filesystem::path scene_path(path);
        if(scene_path.empty()) {
            throw std::runtime_error("Scene path cannot be empty");
        }
        write_text_file_atomic(scene_path, serialize(scene));
    }

    std::unique_ptr<Scene> SceneSerializer::load(const std::string& path) const {
        return deserialize(read_text_file(path), path);
    }
}

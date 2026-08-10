#include "scene/scene_serializer.h"

#include "scene/components.h"
#include "scene/scene.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace Comet {
    namespace {
        struct EntityRecord {
            EntityUuid uuid;
            std::optional<EntityUuid> parent;
            std::string name;
            std::optional<TransformComponent> transform;
            std::optional<MeshRendererComponent> mesh_renderer;
            std::optional<CameraComponent> camera;
        };

        std::runtime_error scene_error(const std::string_view source,
                                       const std::string_view location,
                                       const std::string& detail) {
            return std::runtime_error(
                "Invalid scene '" + std::string(source) + "' at '"
                + std::string(location) + "': " + detail);
        }

        void require_map(const YAML::Node& node,
                         const std::string_view source,
                         const std::string_view location) {
            if(!node.IsDefined() || !node.IsMap()) {
                throw scene_error(source, location, "expected a mapping");
            }
        }

        void require_sequence(const YAML::Node& node,
                              const std::string_view source,
                              const std::string_view location) {
            if(!node.IsDefined() || !node.IsSequence()) {
                throw scene_error(source, location, "expected a sequence");
            }
        }

        void validate_keys(const YAML::Node& node,
                           const std::initializer_list<std::string_view> allowed,
                           const std::string_view source,
                           const std::string_view location) {
            require_map(node, source, location);
            std::unordered_set<std::string> keys;
            for(const auto& entry: node) {
                std::string key;
                try {
                    key = entry.first.as<std::string>();
                } catch(const YAML::Exception&) {
                    throw scene_error(source, location, "expected string keys");
                }

                if(!keys.insert(key).second) {
                    throw scene_error(
                        source, location, "duplicate field '" + key + "'");
                }
                if(std::ranges::find(allowed, key) == allowed.end()) {
                    throw scene_error(
                        source, location, "unknown field '" + key + "'");
                }
            }
        }

        YAML::Node required_child(const YAML::Node& node,
                                  const char* key,
                                  const std::string_view source,
                                  const std::string_view location) {
            const YAML::Node child = node[key];
            if(!child.IsDefined()) {
                throw scene_error(
                    source,
                    location,
                    "missing required field '" + std::string(key) + "'");
            }
            return child;
        }

        template<typename T>
        T read_scalar(const YAML::Node& node,
                      const std::string_view source,
                      const std::string_view location,
                      const std::string_view expected) {
            if(!node.IsDefined() || !node.IsScalar()) {
                throw scene_error(
                    source, location, "expected " + std::string(expected));
            }
            try {
                return node.as<T>();
            } catch(const YAML::Exception&) {
                throw scene_error(
                    source, location, "expected " + std::string(expected));
            }
        }

        template<typename T>
        T read_unsigned_integer(const YAML::Node& node,
                                const std::string_view source,
                                const std::string_view location) {
            if(!node.IsDefined() || !node.IsScalar()) {
                throw scene_error(
                    source, location, "expected a non-negative integer");
            }

            const std::string text = node.Scalar();
            T value{};
            const auto [end, error] = std::from_chars(
                text.data(), text.data() + text.size(), value);
            if(error != std::errc{} || end != text.data() + text.size()) {
                throw scene_error(
                    source, location, "expected a non-negative integer");
            }
            return value;
        }

        EntityUuid read_uuid(const YAML::Node& node,
                             const std::string_view source,
                             const std::string_view location) {
            const std::string value = read_scalar<std::string>(
                node, source, location, "a UUID string");
            const auto uuid = EntityUuid::parse(value);
            if(!uuid || !*uuid) {
                throw scene_error(
                    source, location, "expected a non-nil canonical UUID");
            }
            return *uuid;
        }

        Math::Vec3 read_vec3(const YAML::Node& node,
                             const std::string_view source,
                             const std::string_view location) {
            require_sequence(node, source, location);
            if(node.size() != 3) {
                throw scene_error(
                    source, location, "expected exactly three numbers");
            }

            Math::Vec3 value;
            for(std::size_t index = 0; index < 3; ++index) {
                value[index] = read_scalar<float>(
                    node[index], source,
                    std::string(location) + "[" + std::to_string(index) + "]",
                    "a finite number");
                if(!std::isfinite(value[index])) {
                    throw scene_error(
                        source, location, "expected finite numbers");
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

        std::string entity_location(const std::size_t index) {
            return "entities[" + std::to_string(index) + "]";
        }

        EntityRecord read_entity_record(const YAML::Node& node,
                                        const std::size_t index,
                                        const std::string_view source) {
            const std::string location = entity_location(index);
            validate_keys(
                node, {"uuid", "parent", "components"}, source, location);

            EntityRecord record;
            record.uuid = read_uuid(
                required_child(node, "uuid", source, location),
                source, location + ".uuid");

            const YAML::Node parent = node["parent"];
            if(parent.IsDefined() && !parent.IsNull()) {
                record.parent = read_uuid(
                    parent, source, location + ".parent");
            }

            const YAML::Node components = required_child(
                node, "components", source, location);
            validate_keys(
                components,
                {"name", "transform", "mesh_renderer", "camera"},
                source,
                location + ".components");

            record.name = read_scalar<std::string>(
                required_child(
                    components, "name", source, location + ".components"),
                source, location + ".components.name", "a string");

            const YAML::Node transform = components["transform"];
            if(transform.IsDefined() && !transform.IsNull()) {
                validate_keys(
                    transform,
                    {"translation", "rotation", "scale"},
                    source,
                    location + ".components.transform");
                record.transform = TransformComponent{
                    .translation = read_vec3(
                        required_child(
                            transform,
                            "translation",
                            source,
                            location + ".components.transform"),
                        source,
                        location + ".components.transform.translation"),
                    .rotation = read_vec3(
                        required_child(
                            transform,
                            "rotation",
                            source,
                            location + ".components.transform"),
                        source,
                        location + ".components.transform.rotation"),
                    .scale = read_vec3(
                        required_child(
                            transform,
                            "scale",
                            source,
                            location + ".components.transform"),
                        source,
                        location + ".components.transform.scale")
                };
            }

            const YAML::Node mesh_renderer = components["mesh_renderer"];
            if(mesh_renderer.IsDefined() && !mesh_renderer.IsNull()) {
                validate_keys(
                    mesh_renderer,
                    {"mesh", "material"},
                    source,
                    location + ".components.mesh_renderer");
                record.mesh_renderer = MeshRendererComponent{
                    .mesh = AssetHandle(read_unsigned_integer<
                        AssetHandle::ValueType>(
                        required_child(
                            mesh_renderer,
                            "mesh",
                            source,
                            location + ".components.mesh_renderer"),
                        source,
                        location + ".components.mesh_renderer.mesh")),
                    .material = AssetHandle(read_unsigned_integer<
                        AssetHandle::ValueType>(
                        required_child(
                            mesh_renderer,
                            "material",
                            source,
                            location + ".components.mesh_renderer"),
                        source,
                        location + ".components.mesh_renderer.material"))
                };
            }

            const YAML::Node camera = components["camera"];
            if(camera.IsDefined() && !camera.IsNull()) {
                validate_keys(
                    camera,
                    {"primary", "fov", "near_clip", "far_clip"},
                    source,
                    location + ".components.camera");
                record.camera = CameraComponent{
                    .primary = read_scalar<bool>(
                        required_child(
                            camera,
                            "primary",
                            source,
                            location + ".components.camera"),
                        source,
                        location + ".components.camera.primary",
                        "a boolean"),
                    .fov = read_scalar<float>(
                        required_child(
                            camera,
                            "fov",
                            source,
                            location + ".components.camera"),
                        source,
                        location + ".components.camera.fov",
                        "a finite number"),
                    .near_clip = read_scalar<float>(
                        required_child(
                            camera,
                            "near_clip",
                            source,
                            location + ".components.camera"),
                        source,
                        location + ".components.camera.near_clip",
                        "a finite number"),
                    .far_clip = read_scalar<float>(
                        required_child(
                            camera,
                            "far_clip",
                            source,
                            location + ".components.camera"),
                        source,
                        location + ".components.camera.far_clip",
                        "a finite number")
                };

                if(!std::isfinite(record.camera->fov)
                   || !std::isfinite(record.camera->near_clip)
                   || !std::isfinite(record.camera->far_clip)) {
                    throw scene_error(
                        source,
                        location + ".components.camera",
                        "camera values must be finite");
                }
            }

            return record;
        }

        void validate_records(const std::vector<EntityRecord>& records,
                              const std::string_view source) {
            std::unordered_map<EntityUuid, std::size_t> indices;
            indices.reserve(records.size());
            for(std::size_t index = 0; index < records.size(); ++index) {
                const EntityRecord& record = records[index];
                if(!indices.emplace(records[index].uuid, index).second) {
                    throw scene_error(
                        source,
                        entity_location(index) + ".uuid",
                        "duplicate UUID " + records[index].uuid.to_string());
                }

                const auto finite_vec3 = [](const Math::Vec3& value) {
                    return std::isfinite(value.x)
                           && std::isfinite(value.y)
                           && std::isfinite(value.z);
                };
                if(record.transform
                   && (!finite_vec3(record.transform->translation)
                       || !finite_vec3(record.transform->rotation)
                       || !finite_vec3(record.transform->scale))) {
                    throw scene_error(
                        source,
                        entity_location(index) + ".components.transform",
                        "transform values must be finite");
                }
                if(record.camera
                   && (!std::isfinite(record.camera->fov)
                       || !std::isfinite(record.camera->near_clip)
                       || !std::isfinite(record.camera->far_clip))) {
                    throw scene_error(
                        source,
                        entity_location(index) + ".components.camera",
                        "camera values must be finite");
                }
            }

            for(std::size_t index = 0; index < records.size(); ++index) {
                if(records[index].parent
                   && !indices.contains(*records[index].parent)) {
                    throw scene_error(
                        source,
                        entity_location(index) + ".parent",
                        "missing parent UUID "
                        + records[index].parent->to_string());
                }
            }

            enum class VisitState { Visiting, Complete };
            std::unordered_map<EntityUuid, VisitState> states;
            states.reserve(records.size());
            const auto visit = [&records, &indices, &states, source](
                const std::size_t index, const auto& visit_ref) -> void {
                const EntityUuid uuid = records[index].uuid;
                if(const auto state = states.find(uuid); state != states.end()) {
                    if(state->second == VisitState::Visiting) {
                        throw scene_error(
                            source,
                            entity_location(index) + ".parent",
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
            if(record.transform) {
                YAML::Node transform(YAML::NodeType::Map);
                transform["translation"] = write_vec3(
                    record.transform->translation);
                transform["rotation"] = write_vec3(record.transform->rotation);
                transform["scale"] = write_vec3(record.transform->scale);
                components["transform"] = transform;
            }
            if(record.mesh_renderer) {
                YAML::Node mesh_renderer(YAML::NodeType::Map);
                mesh_renderer["mesh"] = record.mesh_renderer->mesh.value();
                mesh_renderer["material"] =
                    record.mesh_renderer->material.value();
                components["mesh_renderer"] = mesh_renderer;
            }
            if(record.camera) {
                YAML::Node camera(YAML::NodeType::Map);
                camera["primary"] = record.camera->primary;
                camera["fov"] = record.camera->fov;
                camera["near_clip"] = record.camera->near_clip;
                camera["far_clip"] = record.camera->far_clip;
                components["camera"] = camera;
            }
            entity["components"] = components;
            return entity;
        }
    }

    std::string SceneSerializer::serialize(const Scene& scene) const {
        std::vector<EntityRecord> records;
        records.reserve(scene.entity_count());

        std::unordered_map<EntityId, EntityUuid> uuids_by_id;
        uuids_by_id.reserve(scene.entity_count());
        std::unordered_map<EntityUuid, entt::entity> handles_by_uuid;
        handles_by_uuid.reserve(scene.entity_count());
        const auto entities = scene.m_registry.view<IdComponent>();
        for(const entt::entity handle: entities) {
            const auto* uuid = scene.m_registry.try_get<UuidComponent>(handle);
            const auto* name = scene.m_registry.try_get<NameComponent>(handle);
            const EntityId id = entities.get<IdComponent>(handle).id;
            if(!uuid || !uuid->uuid) {
                throw scene_error(
                    "<memory>", "entities", "entity has no valid UUID");
            }
            if(!name) {
                throw scene_error(
                    "<memory>", uuid->uuid.to_string(),
                    "entity has no NameComponent");
            }
            if(!uuids_by_id.emplace(id, uuid->uuid).second) {
                throw scene_error(
                    "<memory>", uuid->uuid.to_string(),
                    "duplicate runtime EntityId " + std::to_string(id));
            }
            if(!handles_by_uuid.emplace(uuid->uuid, handle).second) {
                throw scene_error(
                    "<memory>", uuid->uuid.to_string(), "duplicate UUID");
            }

            EntityRecord record{
                .uuid = uuid->uuid,
                .name = name->name
            };
            if(const auto* transform =
                   scene.m_registry.try_get<TransformComponent>(handle)) {
                record.transform = *transform;
            }
            if(const auto* mesh_renderer =
                   scene.m_registry.try_get<MeshRendererComponent>(handle)) {
                record.mesh_renderer = *mesh_renderer;
            }
            if(const auto* camera =
                   scene.m_registry.try_get<CameraComponent>(handle)) {
                record.camera = *camera;
            }
            records.push_back(std::move(record));
        }

        for(std::size_t index = 0; index < records.size(); ++index) {
            const auto* relationship =
                scene.m_registry.try_get<RelationshipComponent>(
                    handles_by_uuid.at(records[index].uuid));
            if(!relationship
               || relationship->parent == INVALID_ENTITY_ID) {
                continue;
            }
            const auto parent = uuids_by_id.find(relationship->parent);
            if(parent == uuids_by_id.end()) {
                throw scene_error(
                    "<memory>", records[index].uuid.to_string(),
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
        for(const EntityRecord& record: records) {
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
                "Failed to parse scene '" + std::string(source)
                + "': " + error.what());
        }

        validate_keys(root, {"version", "entities"}, source, "<root>");
        const std::uint32_t version = read_unsigned_integer<std::uint32_t>(
            required_child(root, "version", source, "<root>"),
            source, "version");
        if(version != FORMAT_VERSION) {
            throw scene_error(
                source,
                "version",
                "unsupported version " + std::to_string(version)
                + "; expected " + std::to_string(FORMAT_VERSION));
        }

        const YAML::Node entities = required_child(
            root, "entities", source, "<root>");
        require_sequence(entities, source, "entities");
        std::vector<EntityRecord> records;
        records.reserve(entities.size());
        for(std::size_t index = 0; index < entities.size(); ++index) {
            records.push_back(read_entity_record(entities[index], index, source));
        }
        validate_records(records, source);

        auto scene = std::make_unique<Scene>();
        std::unordered_map<EntityUuid, Entity> loaded_entities;
        loaded_entities.reserve(records.size());
        for(const EntityRecord& record: records) {
            Entity entity = scene->create_entity_with_uuid(
                record.uuid, record.name);
            if(!entity) {
                throw scene_error(
                    source, record.uuid.to_string(),
                    "failed to create entity");
            }
            entity.get_component<NameComponent>().name = record.name;
            if(record.transform) {
                entity.get_component<TransformComponent>() = *record.transform;
            } else {
                entity.remove_component<TransformComponent>();
            }
            if(record.mesh_renderer) {
                entity.add_component<MeshRendererComponent>(
                    *record.mesh_renderer);
            }
            if(record.camera) {
                entity.add_component<CameraComponent>(*record.camera);
            }
            loaded_entities.emplace(record.uuid, entity);
        }

        for(const EntityRecord& record: records) {
            if(!record.parent) {
                continue;
            }
            if(!scene->set_parent(
                   loaded_entities.at(record.uuid),
                   loaded_entities.at(*record.parent))) {
                throw scene_error(
                    source, record.uuid.to_string(),
                    "failed to restore parent relationship");
            }
        }
        scene->update_world_transforms();
        return scene;
    }

    void SceneSerializer::save(
        const Scene& scene, const std::string& path) const {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if(!output.is_open()) {
            throw std::runtime_error("Failed to open scene for writing: " + path);
        }
        output << serialize(scene);
        if(!output) {
            throw std::runtime_error("Failed to write scene: " + path);
        }
    }

    std::unique_ptr<Scene> SceneSerializer::load(
        const std::string& path) const {
        std::ifstream input(path, std::ios::binary);
        if(!input.is_open()) {
            throw std::runtime_error("Scene file not found: " + path);
        }

        std::ostringstream contents;
        contents << input.rdbuf();
        if(input.bad()) {
            throw std::runtime_error("Failed to read scene: " + path);
        }
        return deserialize(contents.str(), path);
    }
}

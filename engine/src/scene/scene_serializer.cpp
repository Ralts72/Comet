#include "scene/scene_serializer.h"
#include "scene/prefab.h"
#include "scene/scene_format.h"

#include "common/file_io.h"
#include "common/json.h"
#include "scene/component_registry.h"
#include "scene/components.h"
#include "scene/scene.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Comet {
    using namespace SceneFormat;

    struct SceneSerializer::ContentSnapshot {
        SceneEnvironment environment;
        PostProcessSettings post_process;
        std::vector<EntityRecord> entities;
    };

    SceneSerializer::SceneSerializer(const ComponentRegistry& component_registry, Sources sources)
        : m_component_registry(component_registry), m_sources(std::move(sources)) {}

    Result<SceneSerializer::ContentSnapshot> SceneSerializer::capture_content(
        const Scene& scene, const std::size_t prefab_depth) const {
        if(prefab_depth > MAX_PREFAB_DEPTH)
            return Result<ContentSnapshot>::failure("Maximum prefab nesting depth exceeded");
        const Json::Context context("scene", "<memory>");
        ContentSnapshot content{
            .environment = scene.get_environment(), .post_process = scene.get_post_process()};
        auto& records = content.entities;
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
                return Result<ContentSnapshot>::failure(
                    context.error("entities", "entity has no valid UUID"));
            }
            if(!name) {
                return Result<ContentSnapshot>::failure(
                    context.error(uuid->uuid.to_string(), "entity has no NameComponent"));
            }
            if(!simdjson::validate_utf8(name->name))
                return Result<ContentSnapshot>::failure(
                    context.error(uuid->uuid.to_string() + ".name", "invalid UTF-8"));
            if(!uuids_by_id.emplace(id, uuid->uuid).second) {
                return Result<ContentSnapshot>::failure(context.error(
                    uuid->uuid.to_string(), "duplicate runtime EntityId " + std::to_string(id)));
            }
            if(!handles_by_uuid.emplace(uuid->uuid, handle).second) {
                return Result<ContentSnapshot>::failure(
                    context.error(uuid->uuid.to_string(), "duplicate UUID"));
            }

            EntityRecord record{.uuid = uuid->uuid, .name = name->name};
            const Entity entity(handle, const_cast<Scene*>(&scene));
            if(entity.has_component<PrefabInstanceComponent>()) {
                const auto& instance = entity.get_component<PrefabInstanceComponent>();
                if(!instance.asset || !instance.base)
                    return Result<ContentSnapshot>::failure("Prefab instance has no source");
                Scene base;
                if(!EntityTrees::restore(base, m_component_registry, instance.base->m_entities))
                    return Result<ContentSnapshot>::failure("Cannot restore prefab baseline");
                auto captured = capture_content(base, prefab_depth + 1);
                if(!captured)
                    return Result<ContentSnapshot>::failure(captured.error());
                record.prefab = std::make_shared<PrefabRecord>(PrefabRecord{instance.asset,
                    instance.entities, std::move(captured).value().entities, instance.source_type});
            }
            for(const ComponentDescriptor& component_descriptor :
                m_component_registry.components()) {
                if(!component_descriptor.serializable
                    || !component_descriptor.has_component(entity)) {
                    continue;
                }

                if(component_descriptor.id == "name"
                    || !simdjson::validate_utf8(component_descriptor.id))
                    return Result<ContentSnapshot>::failure(
                        context.error(uuid->uuid.to_string(), "invalid component identifier"));
                const void* component = component_descriptor.get_component(entity);
                const std::string component_location =
                    uuid->uuid.to_string() + ".components." + component_descriptor.id;
                if(component == nullptr) {
                    return Result<ContentSnapshot>::failure(
                        context.error(component_location, "component accessor returned null"));
                }

                ComponentRecord component_record{.descriptor = &component_descriptor};
                component_record.properties.reserve(component_descriptor.properties.size());
                for(const PropertyDescriptor& property : component_descriptor.properties) {
                    if(!property.serializable || property.transient) {
                        continue;
                    }
                    if(!simdjson::validate_utf8(property.id))
                        return Result<ContentSnapshot>::failure(
                            context.error(component_location, "invalid property identifier"));
                    auto value = copy_property_value(
                        property, component, context, component_location + "." + property.id);
                    if(!value)
                        return Result<ContentSnapshot>::failure(value.error());
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
                return Result<ContentSnapshot>::failure(
                    context.error(records[index].uuid.to_string(),
                        "relationship references missing runtime parent "
                            + std::to_string(relationship->parent)));
            }
            records[index].parent = parent->second;
        }

        std::ranges::sort(records, {}, &EntityRecord::uuid);
        if(auto valid = validate_records(records, context); !valid)
            return Result<ContentSnapshot>::failure(valid.error());
        return Result<ContentSnapshot>::success(std::move(content));
    }

    Result<std::string> SceneSerializer::serialize(const Scene& scene) const {
        auto captured = capture_content(scene);
        if(!captured)
            return Result<std::string>::failure(captured.error());
        return serialize_content(captured.value(), true);
    }

    Result<std::string> SceneSerializer::serialize_entities(const Scene& scene) const {
        auto captured = capture_content(scene);
        if(!captured)
            return Result<std::string>::failure(captured.error());
        return serialize_content(captured.value(), false);
    }

    Result<std::string> SceneSerializer::serialize_content(
        const ContentSnapshot& content, bool include_scene_settings) const {
        const auto& records = content.entities;
        ChildrenIndex children;
        for(std::size_t index = 0; index < records.size(); ++index)
            children[records[index].parent.value_or(INVALID_ENTITY_UUID)].push_back(index);

        Json::Writer writer;
        writer.begin_object();
        writer.field("version", std::uint64_t(FORMAT_VERSION));
        if(include_scene_settings) {
            const auto& environment = content.environment;
            writer.key("environment");
            writer.begin_object();
            writer.field("asset", environment.asset.value());
            writer.field("background", environment.background);
            writer.field("intensity", environment.intensity);
            writer.field("rotation", environment.rotation);
            writer.field("lighting", environment.lighting);
            writer.field("lighting_intensity", environment.lighting_intensity);
            writer.key("background_color");
            Json::write_vector(writer, environment.background_color);
            writer.field("fog_enabled", environment.fog_enabled);
            writer.key("fog_color");
            Json::write_vector(writer, environment.fog_color);
            writer.field("fog_start", environment.fog_start);
            writer.field("fog_end", environment.fog_end);
            writer.field("realtime_gi_budget", uint64_t(environment.realtime_gi_budget));
            if(environment.volumetric_fog != VolumetricFogSettings{}) {
                writer.key("volumetric_fog");
                write_volumetric_fog(writer, environment.volumetric_fog);
            }
            writer.end_object();
            const auto& post_process = content.post_process;
            writer.key("post_process");
            writer.begin_object();
            writer.field("exposure", post_process.exposure);
            writer.field("bloom_enabled", post_process.bloom_enabled);
            writer.field("bloom_strength", post_process.bloom_strength);
            writer.field("bloom_threshold", post_process.bloom_threshold);
            writer.field("ao_enabled", post_process.ao_enabled);
            writer.field("weighted_oit", post_process.weighted_oit);
            writer.field("ao_radius", post_process.ao_radius);
            writer.field("ao_strength", post_process.ao_strength);
            if(post_process.reflections != ScreenSpaceReflectionSettings{}) {
                const auto& reflections = post_process.reflections;
                writer.key("screen_space_reflections");
                writer.begin_object();
                writer.field("enabled", reflections.enabled);
                writer.field("strength", reflections.strength);
                writer.field("max_distance", reflections.max_distance);
                writer.field("thickness", reflections.thickness);
                writer.field("max_roughness", reflections.max_roughness);
                writer.end_object();
            }
            writer.end_object();
        }
        writer.key("entities");
        writer.begin_array();
        for(const auto index : children[INVALID_ENTITY_UUID]) {
            if(auto result =
                    write_scene_tree(writer, records[index], records, children, 1, bool(m_sources));
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
        if(version.value() != 2 && version.value() != FORMAT_VERSION) {
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
        ContentSnapshot content;
        auto& records = content.entities;
        records.reserve(entities.value().size());
        std::unordered_set<EntityUuid> uuids;
        std::size_t index = 0;
        for(const auto entity : entities.value()) {
            auto result = read_entity_tree(entity, std::nullopt, entity_location(index++), context,
                m_component_registry, records, uuids, 1);
            if(!result)
                return LoadResult::failure(result.error());
        }

        Json::Node environment_node;
        if(const auto error = root["environment"].get(environment_node);
            error != simdjson::NO_SUCH_FIELD) {
            if(error)
                return LoadResult::failure(context.error("environment", "invalid object"));
            if(auto valid = context.validate_keys(environment_node,
                   {"asset", "background", "intensity", "rotation", "lighting",
                       "lighting_intensity", "background_color", "fog_enabled", "fog_color",
                       "fog_start", "fog_end", "volumetric_fog", "realtime_gi_budget"},
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
                auto color = Json::read_vector<Math::Vec3>(
                    color_node, context, "environment.background_color");
                if(!color)
                    return LoadResult::failure(color.error());
                environment.background_color = color.value();
            }
            const auto fog_enabled = context.read_optional_field<bool>(
                environment_node, "fog_enabled", "a boolean", "environment");
            if(!fog_enabled)
                return LoadResult::failure(fog_enabled.error());
            environment.fog_enabled = fog_enabled.value().value_or(environment.fog_enabled);
            for(const auto& [key, target] : {std::pair{"fog_start", &environment.fog_start},
                    std::pair{"fog_end", &environment.fog_end}}) {
                const auto value = context.read_optional_field<float>(
                    environment_node, key, "a finite number", "environment");
                if(!value)
                    return LoadResult::failure(value.error());
                *target = value.value().value_or(*target);
            }
            if(const auto error = environment_node["fog_color"].get(color_node);
                error != simdjson::NO_SUCH_FIELD) {
                if(error)
                    return LoadResult::failure(
                        context.error("environment.fog_color", "invalid color"));
                auto color =
                    Json::read_vector<Math::Vec3>(color_node, context, "environment.fog_color");
                if(!color)
                    return LoadResult::failure(color.error());
                environment.fog_color = color.value();
            }
            if(environment_node["realtime_gi_budget"].error() != simdjson::NO_SUCH_FIELD) {
                auto budget = context.read_field<uint32_t>(
                    environment_node, "realtime_gi_budget", "an integer", "environment");
                if(!budget)
                    return LoadResult::failure(budget.error());
                environment.realtime_gi_budget = budget.value();
            }
            const auto volumetric = context.find(environment_node, "volumetric_fog");
            if(!volumetric)
                return LoadResult::failure(volumetric.error());
            if(volumetric.value()) {
                auto parsed = read_volumetric_fog(*volumetric.value(), context);
                if(!parsed)
                    return LoadResult::failure(parsed.error());
                environment.volumetric_fog = parsed.value();
            }
            content.environment = environment;
        }
        Json::Node post_process_node;
        if(const auto error = root["post_process"].get(post_process_node);
            error != simdjson::NO_SUCH_FIELD) {
            if(error)
                return LoadResult::failure(context.error("post_process", "invalid object"));
            if(auto valid = context.validate_keys(post_process_node,
                   {"exposure", "bloom_enabled", "bloom_strength", "bloom_threshold", "ao_enabled",
                       "ao_radius", "ao_strength", "screen_space_reflections", "weighted_oit"},
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
            PostProcessSettings settings{
                exposure.value(), enabled.value(), strength.value(), threshold.value()};
            const auto weighted = context.read_optional_field<bool>(
                post_process_node, "weighted_oit", "a boolean", "post_process");
            if(!weighted)
                return LoadResult::failure(weighted.error());
            settings.weighted_oit = weighted.value().value_or(false);
            const auto ao_enabled = context.read_optional_field<bool>(
                post_process_node, "ao_enabled", "a boolean", "post_process");
            const auto ao_radius = context.read_optional_field<float>(
                post_process_node, "ao_radius", "a finite number", "post_process");
            const auto ao_strength = context.read_optional_field<float>(
                post_process_node, "ao_strength", "a finite number", "post_process");
            if(!ao_enabled)
                return LoadResult::failure(ao_enabled.error());
            if(!ao_radius)
                return LoadResult::failure(ao_radius.error());
            if(!ao_strength)
                return LoadResult::failure(ao_strength.error());
            settings.ao_enabled = ao_enabled.value().value_or(settings.ao_enabled);
            settings.ao_radius = ao_radius.value().value_or(settings.ao_radius);
            settings.ao_strength = ao_strength.value().value_or(settings.ao_strength);
            const auto reflections = context.find(post_process_node, "screen_space_reflections");
            if(!reflections)
                return LoadResult::failure(reflections.error());
            if(reflections.value()) {
                constexpr std::string_view location = "post_process.screen_space_reflections";
                const auto node = *reflections.value();
                if(auto valid = context.validate_keys(node,
                       {"enabled", "strength", "max_distance", "thickness", "max_roughness"},
                       location);
                    !valid)
                    return LoadResult::failure(valid.error());
                auto& value = settings.reflections;
                const auto enabled =
                    context.read_optional_field<bool>(node, "enabled", "a boolean", location);
                if(!enabled)
                    return LoadResult::failure(enabled.error());
                value.enabled = enabled.value().value_or(value.enabled);
                for(const auto& [key, target] :
                    {std::pair{"strength", &value.strength}, {"max_distance", &value.max_distance},
                        {"thickness", &value.thickness}, {"max_roughness", &value.max_roughness}}) {
                    const auto number =
                        context.read_optional_field<float>(node, key, "a finite number", location);
                    if(!number)
                        return LoadResult::failure(number.error());
                    *target = number.value().value_or(*target);
                }
            }
            content.post_process = settings;
        }
        if(auto expanded = expand_instances(content, source); !expanded)
            return LoadResult::failure(expanded.error());
        return restore_content(content, source);
    }

    Result<void> SceneSerializer::expand_instances(
        ContentSnapshot& content, std::string_view source) const {
        const Json::Context context("scene", source);
        std::map<std::pair<AssetHandle, AssetType>, std::shared_ptr<const Prefab>> sources;
        std::unordered_set<EntityUuid> removed_by_source;
        const auto expand = [&](auto&& self, std::vector<EntityRecord>& records,
                                size_t depth) -> Result<void> {
            if(depth > MAX_PREFAB_DEPTH)
                return Result<void>::failure(
                    context.error("prefab", "maximum nesting depth exceeded"));
            std::vector<EntityRecord> expanded;
            for(const auto& record : records) {
                if(!record.prefab || !record.prefab->compact) {
                    expanded.push_back(record);
                    continue;
                }
                const auto& saved = *record.prefab;
                std::shared_ptr<const Prefab> incoming;
                if(!saved.base.empty()) {
                    auto scene =
                        restore_content(ContentSnapshot{.entities = saved.base}, source, depth + 1);
                    if(!scene)
                        return Result<void>::failure(scene.error());
                    auto snapshot = EntityTrees::capture(*scene.value(), m_component_registry,
                        scene.value()->get_root_entities().front());
                    if(!snapshot)
                        return Result<void>::failure("Cannot capture embedded prefab source");
                    incoming = std::shared_ptr<const Prefab>(new Prefab(std::move(*snapshot)));
                } else {
                    const auto key = std::pair{saved.asset, saved.source_type};
                    auto found = sources.find(key);
                    if(found == sources.end()) {
                        if(!m_sources)
                            return Result<void>::failure(
                                context.error("prefab", "project source reader is required"));
                        auto loaded = m_sources(saved.asset, saved.source_type);
                        if(!loaded || !loaded.value())
                            return Result<void>::failure(context.error("prefab",
                                loaded ? "source reader returned no content" : loaded.error()));
                        found = sources.emplace(key, std::move(loaded).value()).first;
                    }
                    incoming = found->second;
                }
                auto mapping = saved.entities;
                const auto root_mapping = mapping.find(incoming->root_uuid());
                if(root_mapping == mapping.end() || root_mapping->second != record.uuid
                    || saved.removed.contains(incoming->root_uuid()))
                    return Result<void>::failure(
                        context.error("prefab", "source root identity changed"));
                Scene fresh_scene;
                if(!EntityTrees::restore(fresh_scene, m_component_registry, incoming->m_entities))
                    return Result<void>::failure("Cannot restore incoming prefab source");
                auto fresh = capture_content(fresh_scene, depth + 1);
                if(!fresh)
                    return Result<void>::failure(fresh.error());
                auto before = fresh.value().entities;
                std::unordered_set<EntityUuid> source_ids, targets;
                for(const auto& [id, target] : mapping)
                    targets.insert(target);
                for(const auto& entity : before) {
                    source_ids.insert(entity.uuid);
                    if(!mapping.contains(entity.uuid)) {
                        const auto target = Prefab::member_uuid(record.uuid, entity.uuid);
                        if(!targets.insert(target).second)
                            return Result<void>::failure(
                                "New prefab entity identity conflicts with the instance");
                        mapping.emplace(entity.uuid, target);
                    }
                }
                // 只为源已删除且未编辑的旧成员构造占位，沿既有合并检查引用后移除。
                for(const auto& [id, target] : saved.entities)
                    if(!source_ids.contains(id)) {
                        before.push_back({.uuid = id, .parent = incoming->root_uuid()});
                        if(!saved.removed.contains(id))
                            removed_by_source.insert(target);
                    }
                for(const auto& [original, value, inherit_prefab] : saved.changes) {
                    const auto found =
                        std::ranges::find(before, original.uuid, &EntityRecord::uuid);
                    if(found == before.end())
                        return Result<void>::failure("Override source identity is missing");
                    auto link = found->prefab;
                    *found = original;
                    if(inherit_prefab)
                        found->prefab = std::move(link);
                }
                auto baseline_scene =
                    restore_content(ContentSnapshot{.entities = before}, source, depth + 1);
                if(!baseline_scene)
                    return Result<void>::failure(baseline_scene.error());
                auto baseline = EntityTrees::capture(*baseline_scene.value(), m_component_registry,
                    baseline_scene.value()->get_root_entities().front());
                if(!baseline)
                    return Result<void>::failure("Cannot capture prefab merge baseline");
                std::unordered_set<EntityUuid> deleted = saved.removed;
                bool changed = true;
                while(changed) {
                    changed = false;
                    for(const auto& entity : before)
                        if(entity.parent && deleted.contains(*entity.parent))
                            changed |= deleted.insert(entity.uuid).second;
                }
                std::vector<EntityRecord> actual;
                for(const auto& entity : before)
                    if(!deleted.contains(entity.uuid))
                        actual.push_back(remap_record(entity, mapping));
                for(const auto& [original, value, inherit_prefab] : saved.changes) {
                    const auto found = std::ranges::find(actual, value.uuid, &EntityRecord::uuid);
                    if(found == actual.end())
                        return Result<void>::failure("Override targets a removed entity");
                    auto link = found->prefab;
                    *found = value;
                    if(inherit_prefab)
                        found->prefab = std::move(link);
                }
                auto additions = saved.added;
                if(auto ready = self(self, additions, depth + 1); !ready)
                    return ready;
                actual.insert(actual.end(), additions.begin(), additions.end());
                auto candidate = restore_content(
                    ContentSnapshot{.entities = std::move(actual)}, source, depth + 1);
                if(!candidate)
                    return Result<void>::failure(candidate.error());
                auto root = candidate.value()->find_entity(record.uuid);
                root.add_component<PrefabInstanceComponent>(PrefabInstanceComponent{saved.asset,
                    std::shared_ptr<const Prefab>(new Prefab(std::move(*baseline))), mapping,
                    saved.source_type});
                // 属性与结构的来源更新继续由 Prefab 领域处理，不另造一套 JSON 合并规则。
                auto merged =
                    incoming->update_candidate(*candidate.value(), m_component_registry, root);
                if(!merged)
                    return Result<void>::failure(context.error("prefab", merged.error()));
                Scene resolved;
                if(!EntityTrees::restore(resolved, m_component_registry, merged.value()))
                    return Result<void>::failure("Cannot restore merged prefab instance");
                resolved.find_entity(record.uuid).get_component<NameComponent>().name = record.name;
                auto captured = capture_content(resolved, depth + 1);
                if(!captured)
                    return Result<void>::failure(captured.error());
                for(auto& entity : captured.value().entities) {
                    if(entity.uuid == record.uuid)
                        entity.parent = record.parent;
                    expanded.push_back(std::move(entity));
                }
            }
            records = std::move(expanded);
            return Result<void>::success();
        };
        if(auto ready = expand(expand, content.entities, 0); !ready)
            return ready;
        std::unordered_set<EntityUuid> present;
        for(const auto& entity : content.entities)
            present.insert(entity.uuid);
        for(const auto& entity : content.entities)
            for(const auto& component : entity.components)
                for(const auto& property : component.properties) {
                    bool invalid = false;
                    visit_entity_references(property.value, [&](EntityUuid id) {
                        invalid |= removed_by_source.contains(id) && !present.contains(id);
                    });
                    if(invalid)
                        return Result<void>::failure(context.error(entity.uuid.to_string(),
                            "source removed an entity referenced by the scene"));
                }
        return Result<void>::success();
    }

    Result<std::unique_ptr<Scene>> SceneSerializer::restore_content(const ContentSnapshot& content,
        const std::string_view source, const std::size_t prefab_depth) const {
        using LoadResult = Result<std::unique_ptr<Scene>>;
        const Json::Context context("scene", source);
        if(auto valid = validate_records(content.entities, context, prefab_depth); !valid)
            return LoadResult::failure(valid.error());
        if(auto valid = content.environment.validate(); !valid)
            return LoadResult::failure(context.error("environment", valid.error()));
        if(auto valid = content.post_process.validate(); !valid)
            return LoadResult::failure(context.error("post_process", valid.error()));
        auto scene = std::make_unique<Scene>();
        static_cast<void>(scene->set_environment(content.environment));
        scene->m_post_process = content.post_process;
        const auto& records = content.entities;
        ChildrenIndex children;
        for(std::size_t index = 0; index < records.size(); ++index)
            children[records[index].parent.value_or(INVALID_ENTITY_UUID)].push_back(index);
        std::vector<std::size_t> pending;
        const auto& roots = children[INVALID_ENTITY_UUID];
        pending.assign(roots.rbegin(), roots.rend());
        std::vector<std::size_t> order;
        order.reserve(records.size());
        while(!pending.empty()) {
            const auto index = pending.back();
            pending.pop_back();
            order.push_back(index);
            if(const auto found = children.find(records[index].uuid); found != children.end())
                pending.insert(pending.end(), found->second.rbegin(), found->second.rend());
        }

        std::unordered_map<EntityUuid, Entity> loaded_entities;
        loaded_entities.reserve(records.size());
        for(const auto index : order) {
            const EntityRecord& record = records[index];
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
                    if(property.descriptor->type == PropertyType::Enum
                        && std::ranges::none_of(
                            property.descriptor->enum_options, [&](const auto& option) {
                                return option.id == std::get<std::string>(property.value);
                            }))
                        return LoadResult::failure(
                            context.error(component_location + "." + property.descriptor->id,
                                "unknown enum name: " + std::get<std::string>(property.value)));
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
        for(const auto& record : records) {
            if(!record.prefab)
                continue;
            ContentSnapshot base_content{.entities = record.prefab->base};
            auto base_scene = restore_content(base_content, source, prefab_depth + 1);
            if(!base_scene)
                return LoadResult::failure(base_scene.error());
            const auto root = base_scene.value()->get_root_entities().front();
            auto entities = EntityTrees::capture(*base_scene.value(), m_component_registry, root);
            if(!entities
                || !EntityTrees::has_only_internal_references(
                    *base_scene.value(), m_component_registry, *entities))
                return LoadResult::failure("Prefab baseline contains external entity references");
            loaded_entities.at(record.uuid)
                .add_component<PrefabInstanceComponent>(
                    PrefabInstanceComponent{record.prefab->asset,
                        std::shared_ptr<const Prefab>(new Prefab(std::move(*entities))),
                        record.prefab->entities, record.prefab->source_type});
        }
        scene->update_world_transforms();
        return Result<std::unique_ptr<Scene>>::success(std::move(scene));
    }

    Result<std::unique_ptr<Scene>> SceneSerializer::clone(const Scene& scene) const {
        auto content = capture_content(scene);
        if(!content)
            return Result<std::unique_ptr<Scene>>::failure(content.error());
        return restore_content(content.value(), "<scene-clone>");
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

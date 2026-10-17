#include "scene/scene_format.h"
#include "scene/scene_serializer.h"
#include "scene/component_registry.h"
#include "scene/entity_tree.h"
#include "common/serialization/parameters_json.h"
#include <algorithm>
#include <map>

namespace Comet::SceneFormat {
    Result<VolumetricFogSettings> read_volumetric_fog(
        Json::Node node, const Json::Context& context) {
        using Read = Result<VolumetricFogSettings>;
        constexpr std::string_view location = "environment.volumetric_fog";
        if(auto valid = context.validate_keys(node,
               {"enabled", "color", "density", "base_height", "height_falloff", "anisotropy",
                   "max_distance", "ambient"},
               location);
            !valid)
            return Read::failure(valid.error());
        VolumetricFogSettings settings;
        auto enabled = context.read_optional_field<bool>(node, "enabled", "a boolean", location);
        if(!enabled)
            return Read::failure(enabled.error());
        settings.enabled = enabled.value().value_or(false);
        for(const auto& [key, target] : {std::pair{"density", &settings.density},
                std::pair{"base_height", &settings.base_height},
                std::pair{"height_falloff", &settings.height_falloff},
                std::pair{"anisotropy", &settings.anisotropy},
                std::pair{"max_distance", &settings.max_distance},
                std::pair{"ambient", &settings.ambient}}) {
            const auto value =
                context.read_optional_field<float>(node, key, "a finite number", location);
            if(!value)
                return Read::failure(value.error());
            *target = value.value().value_or(*target);
        }
        const auto color = context.find(node, "color");
        if(!color)
            return Read::failure(color.error());
        if(color.value()) {
            auto parsed = Json::read_vector<Math::Vec3>(
                *color.value(), context, "environment.volumetric_fog.color");
            if(!parsed)
                return Read::failure(parsed.error());
            settings.color = parsed.value();
        }
        if(auto valid = settings.validate(); !valid)
            return Read::failure(context.error(location, valid.error()));
        return Read::success(settings);
    }

    void write_volumetric_fog(Json::Writer& writer, const VolumetricFogSettings& settings) {
        writer.begin_object();
        writer.field("enabled", settings.enabled);
        writer.key("color");
        Json::write_vector(writer, settings.color);
        writer.field("density", settings.density);
        writer.field("base_height", settings.base_height);
        writer.field("height_falloff", settings.height_falloff);
        writer.field("anisotropy", settings.anisotropy);
        writer.field("max_distance", settings.max_distance);
        writer.field("ambient", settings.ambient);
        writer.end_object();
    }

    void write_entity_reference(Json::Writer& writer, const EntityUuid value) {
        writer.begin_object();
        writer.field("entity", value.to_string());
        writer.end_object();
    }

    Result<Uuid> read_entity_reference(
        Json::Node node, const Json::Context& context, const std::string_view location) {
        if(auto valid = context.validate_keys(node, {"entity"}, location); !valid)
            return Result<Uuid>::failure(valid.error());
        auto text = context.read_field<std::string>(node, "entity", "a UUID string", location);
        if(!text)
            return Result<Uuid>::failure(text.error());
        const auto uuid = EntityUuid::parse(text.value());
        if(!uuid)
            return Result<Uuid>::failure(
                context.error(location, "expected a canonical entity UUID"));
        return Result<Uuid>::success(*uuid);
    }

    Result<PropertyValue> read_property_value(const PropertyDescriptor& property,
        const Json::Node& node, const Json::Context& context, const std::string_view location) {
        switch(property.type) {
            case PropertyType::Parameters: {
                auto parameters =
                    Json::read_parameters(node, context, location, read_entity_reference);
                if(!parameters)
                    return Result<PropertyValue>::failure(parameters.error());
                return Result<PropertyValue>::success(std::move(parameters).value());
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
            case PropertyType::UInt32: {
                auto value =
                    context.read_scalar<uint32_t>(node, location, "an unsigned 32-bit integer");
                if(!value)
                    return Result<PropertyValue>::failure(value.error());
                return Result<PropertyValue>::success(value.value());
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
                auto value = Json::read_vector<Math::Vec3>(node, context, location);
                if(!value)
                    return Result<PropertyValue>::failure(value.error());
                if(!property.accepts_value(value.value()))
                    return Result<PropertyValue>::failure(
                        context.error(location, "value outside allowed bounds"));
                return Result<PropertyValue>::success(value.value());
            }
            case PropertyType::Vec4: {
                auto value = Json::read_vector<Math::Vec4>(node, context, location);
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
            case PropertyType::EntityReference: {
                auto reference = read_entity_reference(node, context, location);
                if(!reference)
                    return Result<PropertyValue>::failure(reference.error());
                return Result<PropertyValue>::success(reference.value());
            }
        }
        return Result<PropertyValue>::failure(context.error(location, "unsupported property type"));
    }

    Result<PropertyValue> copy_property_value(const PropertyDescriptor& property,
        const void* component, const Json::Context& context, const std::string_view location) {
        auto value = property.copy_value(component);
        if(!value)
            return Result<PropertyValue>::failure(
                context.error(location, "property accessor returned null or unsupported type"));
        if(!property.accepts_value(*value)) {
            const char* message = "value violates property constraints";
            if(property.type == PropertyType::Parameters)
                message = "Invalid parameters";
            else if(property.type == PropertyType::Float || property.type == PropertyType::Vec3
                    || property.type == PropertyType::Vec4)
                message = "expected a finite value within property constraints";
            return Result<PropertyValue>::failure(context.error(location, message));
        }
        if(const auto* text = std::get_if<std::string>(&*value); text && !Json::valid_utf8(*text))
            return Result<PropertyValue>::failure(context.error(location, "invalid UTF-8"));
        if(const auto* parameters = std::get_if<ParameterMap>(&*value)) {
            for(const auto& [name, parameter] : *parameters) {
                const auto* text = std::get_if<std::string>(&parameter);
                if(!Json::valid_utf8(name) || (text && !Json::valid_utf8(*text)))
                    return Result<PropertyValue>::failure(context.error(location, "invalid UTF-8"));
            }
        }
        return Result<PropertyValue>::success(std::move(*value));
    }

    Result<void> write_property_value(Json::Writer& writer, const PropertyRecord& property) {
        return std::visit(
            [&writer](const auto& value) -> Result<void> {
                using T = std::remove_cvref_t<decltype(value)>;
                if constexpr(std::is_same_v<T, Math::Vec3> || std::is_same_v<T, Math::Vec4>)
                    Json::write_vector(writer, value);
                else if constexpr(std::is_same_v<T, AssetHandle>)
                    writer.value(value.value());
                else if constexpr(std::is_same_v<T, uint32_t>)
                    writer.value(uint64_t{value});
                else if constexpr(std::is_same_v<T, EntityUuid>)
                    write_entity_reference(writer, value);
                else if constexpr(std::is_same_v<T, ParameterMap>)
                    return Json::write_parameters(writer, value, Json::Context("scene", "<memory>"),
                        "parameters", write_entity_reference);
                else
                    writer.value(value);
                return Result<void>::success();
            },
            property.value);
    }

    std::string entity_location(const std::size_t index) {
        return "entities[" + std::to_string(index) + "]";
    }

    Result<std::shared_ptr<PrefabRecord>> read_prefab_record(Json::Node node,
        const std::string& location, const Json::Context& context,
        const ComponentRegistry& registry, const std::size_t prefab_depth) {
        using Read = Result<std::shared_ptr<PrefabRecord>>;
        if(auto valid = context.validate_keys(node,
               {"asset", "base", "entities", "type", "changes", "removed", "added"}, location);
            !valid)
            return Read::failure(valid.error());
        auto asset = context.read_field<std::uint64_t>(node, "asset", "an asset handle", location);
        if(!asset)
            return Read::failure(asset.error());
        if(!asset.value())
            return Read::failure(context.error(location, "prefab asset must not be zero"));
        auto result = std::make_shared<PrefabRecord>();
        result->asset = AssetHandle(asset.value());
        if(node["type"].error() != Json::MISSING_FIELD) {
            auto type = context.read_field<std::string>(node, "type", "a source type", location);
            if(!type)
                return Read::failure(type.error());
            if(type.value() == "model")
                result->source_type = AssetType::Model;
            else if(type.value() != "prefab")
                return Read::failure(context.error(location, "invalid prefab source type"));
        }
        result->compact = node["changes"].error() != Json::MISSING_FIELD;
        std::unordered_set<EntityUuid> uuids;
        if(node["base"].error() != Json::MISSING_FIELD) {
            auto base = context.required_child(node, "base", location);
            if(!base)
                return Read::failure(base.error());
            if(auto parsed = read_entity_tree(base.value(), {}, location + ".base", context,
                   registry, result->base, uuids, 1, prefab_depth + 1);
                !parsed)
                return Read::failure(parsed.error());
        } else if(!result->compact)
            return Read::failure(context.error(location, "missing prefab baseline"));
        auto mapping = context.required_child(node, "entities", location);
        if(!mapping)
            return Read::failure(mapping.error());
        std::unordered_set<EntityUuid> targets;
        const auto add_mapping = [&](std::string_view source, std::string_view instance) {
            const auto from = EntityUuid::parse(source), to = EntityUuid::parse(instance);
            return from && *from && to && *to && (result->compact || uuids.contains(*from))
                   && result->entities.emplace(*from, *to).second && targets.insert(*to).second;
        };
        if(result->compact) {
            auto entries = context.object(mapping.value(), location + ".entities");
            if(!entries)
                return Read::failure(entries.error());
            for(const auto entry : entries.value()) {
                auto value = context.read_scalar<std::string>(entry.value, location, "a UUID");
                if(!value || !add_mapping(entry.key, value.value()))
                    return Read::failure(context.error(location, "invalid prefab entity mapping"));
            }
        } else {
            auto entries = context.array(mapping.value(), location + ".entities");
            if(!entries)
                return Read::failure(entries.error());
            for(const auto entry : entries.value()) {
                if(auto valid = context.validate_keys(entry, {"source", "instance"}, location);
                    !valid)
                    return Read::failure(valid.error());
                auto source = context.read_field<std::string>(entry, "source", "a UUID", location);
                auto instance =
                    context.read_field<std::string>(entry, "instance", "a UUID", location);
                if(!source || !instance || !add_mapping(source.value(), instance.value()))
                    return Read::failure(context.error(location, "invalid prefab entity mapping"));
            }
            if(result->entities.size() != result->base.size())
                return Read::failure(context.error(location, "incomplete prefab entity mapping"));
        }
        if(result->compact) {
            const auto read_node = [&](Json::Node wrapper,
                                       std::vector<EntityRecord>& output) -> Result<void> {
                if(auto valid = context.validate_keys(wrapper, {"parent", "entity"}, location);
                    !valid)
                    return valid;
                auto parent =
                    context.read_field<std::string>(wrapper, "parent", "a UUID", location);
                if(!parent)
                    return Result<void>::failure(parent.error());
                const auto uuid = EntityUuid::parse(parent.value());
                if(!uuid)
                    return Result<void>::failure(context.error(location, "invalid parent UUID"));
                auto entity = context.required_child(wrapper, "entity", location);
                if(!entity)
                    return Result<void>::failure(entity.error());
                std::unordered_set<EntityUuid> seen;
                return read_entity_tree(entity.value(), *uuid ? std::optional(*uuid) : std::nullopt,
                    location, context, registry, output, seen, 1, prefab_depth + 1);
            };
            auto changes = context.required_child(node, "changes", location);
            if(!changes)
                return Read::failure(changes.error());
            auto entries = context.array(changes.value(), location + ".changes");
            if(!entries)
                return Read::failure(entries.error());
            std::unordered_set<EntityUuid> changed;
            for(const auto entry : entries.value()) {
                if(auto valid =
                        context.validate_keys(entry, {"base", "value", "inherit_prefab"}, location);
                    !valid)
                    return Read::failure(valid.error());
                std::vector<EntityRecord> before, after;
                for(const auto& [field, output] :
                    {std::pair{"base", &before}, std::pair{"value", &after}}) {
                    auto child = context.required_child(entry, field, location);
                    if(!child)
                        return Read::failure(child.error());
                    if(auto parsed = read_node(child.value(), *output); !parsed)
                        return Read::failure(parsed.error());
                    if(output->size() != 1)
                        return Read::failure(
                            context.error(location, "an override must describe one entity"));
                }
                const auto found = result->entities.find(before.front().uuid);
                if(found == result->entities.end() || found->second != after.front().uuid
                    || !changed.insert(before.front().uuid).second)
                    return Read::failure(
                        context.error(location, "override identity does not match its source"));
                auto inherit = context.read_optional_field<bool>(
                    entry, "inherit_prefab", "a boolean", location);
                if(!inherit)
                    return Read::failure(inherit.error());
                if(inherit.value().value_or(false)
                    && (before.front().prefab || after.front().prefab))
                    return Read::failure(context.error(
                        location, "inherited prefab link must not be embedded in an override"));
                result->changes.push_back({std::move(before.front()), std::move(after.front()),
                    inherit.value().value_or(false)});
            }
            auto removed = context.required_child(node, "removed", location);
            if(!removed)
                return Read::failure(removed.error());
            auto removals = context.array(removed.value(), location + ".removed");
            if(!removals)
                return Read::failure(removals.error());
            for(const auto entry : removals.value()) {
                auto value = context.read_scalar<std::string>(entry, location, "a UUID");
                const auto uuid = value ? EntityUuid::parse(value.value()) : std::nullopt;
                if(!uuid || !result->entities.contains(*uuid) || changed.contains(*uuid)
                    || !result->removed.insert(*uuid).second)
                    return Read::failure(context.error(location, "invalid removed source entity"));
            }
            auto added = context.required_child(node, "added", location);
            if(!added)
                return Read::failure(added.error());
            auto additions = context.array(added.value(), location + ".added");
            if(!additions)
                return Read::failure(additions.error());
            for(const auto entry : additions.value())
                if(auto parsed = read_node(entry, result->added); !parsed)
                    return Read::failure(parsed.error());
        }
        return Read::success(std::move(result));
    }

    Result<EntityRecord> read_entity_record(const Json::Node& node, const std::string& location,
        const Json::Context& context, const ComponentRegistry& component_registry,
        const std::size_t prefab_depth) {
        if(auto valid =
                context.validate_keys(node, {"uuid", "components", "children", "prefab"}, location);
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
        Json::Node prefab;
        if(const auto error = node["prefab"].get(prefab); error != Json::MISSING_FIELD) {
            if(error || prefab_depth >= SceneSerializer::MAX_PREFAB_DEPTH)
                return Result<EntityRecord>::failure(
                    context.error(location, "invalid prefab metadata or nesting depth exceeded"));
            auto parsed = read_prefab_record(
                prefab, location + ".prefab", context, component_registry, prefab_depth);
            if(!parsed)
                return Result<EntityRecord>::failure(parsed.error());
            record.prefab = std::move(parsed).value();
        }

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
        if(auto valid = context.validate_keys(components, component_ids, location + ".components");
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
                if(!property.required && component[property.id].error() == Json::MISSING_FIELD)
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
        std::unordered_set<EntityUuid>& uuids, const std::size_t depth,
        const std::size_t prefab_depth) {
        if(depth > SceneSerializer::MAX_HIERARCHY_DEPTH)
            return Result<void>::failure(
                context.error(location, "maximum hierarchy depth exceeded"));
        auto parsed = read_entity_record(node, location, context, component_registry, prefab_depth);
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
                    component_registry, records, uuids, depth + 1, prefab_depth);
                if(!result)
                    return result;
            }
        }
        return Result<void>::success();
    }

    enum class RecordVisitState { Visiting, Complete };
    struct RecordVisit {
        RecordVisitState state = RecordVisitState::Visiting;
        std::size_t hierarchy_depth = 0;
    };

    Result<void> visit_parent_chain(std::size_t index, std::size_t depth,
        const std::vector<EntityRecord>& records,
        const std::unordered_map<EntityUuid, std::size_t>& indices,
        std::unordered_map<EntityUuid, RecordVisit>& states, const Json::Context& context) {
        if(depth > SceneSerializer::MAX_HIERARCHY_DEPTH)
            return Result<void>::failure(
                context.error(entity_location(index), "maximum hierarchy depth exceeded"));
        const auto& record = records[index];
        const EntityUuid uuid = record.uuid;
        if(const auto state = states.find(uuid); state != states.end()) {
            if(state->second.state == RecordVisitState::Visiting) {
                return Result<void>::failure(context.error(
                    entity_location(index) + ".parent", "parent relationship forms a cycle"));
            }
            return Result<void>::success();
        }

        states.emplace(uuid, RecordVisit{});
        std::size_t hierarchy_depth = 1;
        if(record.parent) {
            const EntityUuid parent = *record.parent;
            if(auto result = visit_parent_chain(
                   indices.at(parent), depth + 1, records, indices, states, context);
                !result)
                return result;
            hierarchy_depth = states.at(parent).hierarchy_depth + 1;
        }
        if(hierarchy_depth > SceneSerializer::MAX_HIERARCHY_DEPTH)
            return Result<void>::failure(
                context.error(entity_location(index), "maximum hierarchy depth exceeded"));
        states[uuid] = {RecordVisitState::Complete, hierarchy_depth};
        return Result<void>::success();
    }

    Result<void> validate_records(const std::vector<EntityRecord>& records,
        const Json::Context& context, const std::size_t prefab_depth) {
        if(prefab_depth > SceneSerializer::MAX_PREFAB_DEPTH)
            return Result<void>::failure("Maximum prefab nesting depth exceeded");
        std::unordered_map<EntityUuid, std::size_t> indices;
        indices.reserve(records.size());
        for(std::size_t index = 0; index < records.size(); ++index) {
            if(!indices.emplace(records[index].uuid, index).second) {
                return Result<void>::failure(context.error(entity_location(index) + ".uuid",
                    "duplicate UUID " + records[index].uuid.to_string()));
            }
        }

        for(std::size_t index = 0; index < records.size(); ++index) {
            if(records[index].parent && !indices.contains(*records[index].parent))
                return Result<void>::failure(context.error(entity_location(index) + ".parent",
                    "missing parent UUID " + records[index].parent->to_string()));
        }
        std::unordered_map<EntityUuid, RecordVisit> states;
        for(std::size_t index = 0; index < records.size(); ++index)
            if(auto result = visit_parent_chain(index, 1, records, indices, states, context);
                !result)
                return result;
        const auto contains = [&](EntityUuid root, EntityUuid child) {
            while(child) {
                if(child == root)
                    return true;
                const auto found = indices.find(child);
                if(found == indices.end())
                    return false;
                child = records[found->second].parent.value_or(EntityUuid{});
            }
            return false;
        };
        std::unordered_map<EntityUuid, std::vector<EntityUuid>> owners;
        for(const auto& record : records) {
            if(!record.prefab)
                continue;
            const auto& prefab = *record.prefab;
            if((prefab.source_type != AssetType::Prefab && prefab.source_type != AssetType::Model)
                || !prefab.asset || prefab.base.empty()
                || prefab.entities.size() != prefab.base.size())
                return Result<void>::failure(
                    context.error(record.uuid.to_string(), "incomplete prefab identity"));
            std::size_t roots = 0;
            std::unordered_set<EntityUuid> members;
            if(auto valid = validate_records(prefab.base, context, prefab_depth + 1); !valid)
                return valid;
            for(const auto& source : prefab.base) {
                const auto found = prefab.entities.find(source.uuid);
                if(found == prefab.entities.end() || !found->second
                    || !members.insert(found->second).second)
                    return Result<void>::failure(context.error(
                        record.uuid.to_string(), "invalid or overlapping prefab identity"));
                auto& previous_owners = owners[found->second];
                for(const auto owner : previous_owners) {
                    if((!contains(owner, record.uuid) && !contains(record.uuid, owner))
                        || (indices.contains(found->second)
                            && (!contains(owner, found->second)
                                || !contains(record.uuid, found->second))))
                        return Result<void>::failure(context.error(record.uuid.to_string(),
                            "prefab identity overlap must follow nested subtrees"));
                }
                previous_owners.push_back(record.uuid);
                if(!source.parent) {
                    ++roots;
                    if(found->second != record.uuid || source.prefab)
                        return Result<void>::failure(context.error(
                            record.uuid.to_string(), "prefab root identity mismatch"));
                }
            }
            if(roots != 1)
                return Result<void>::failure(
                    context.error(record.uuid.to_string(), "prefab must have one root"));
        }

        return Result<void>::success();
    }

    Result<void> write_components(
        Json::Writer& writer, const EntityRecord& record, bool properties = true) {
        writer.key("components");
        writer.begin_object();
        writer.field("name", record.name);
        if(properties)
            for(const ComponentRecord& component_record : record.components) {
                writer.key(component_record.descriptor->id);
                writer.begin_object();
                for(const PropertyRecord& property : component_record.properties) {
                    writer.key(property.descriptor->id);
                    if(auto written = write_property_value(writer, property); !written)
                        return written;
                }
                writer.end_object();
            }
        writer.end_object();
        return Result<void>::success();
    }

    Result<void> write_expanded_tree(Json::Writer& writer, const EntityRecord& record,
        const std::vector<EntityRecord>& records, const ChildrenIndex& children,
        const std::size_t depth) {
        const Json::Context context("scene", "<memory>");
        if(depth > SceneSerializer::MAX_HIERARCHY_DEPTH)
            return Result<void>::failure(
                context.error(record.uuid.to_string(), "maximum hierarchy depth exceeded"));
        writer.begin_object();
        writer.field("uuid", record.uuid.to_string());
        if(record.prefab) {
            writer.key("prefab");
            writer.begin_object();
            writer.field("asset", record.prefab->asset.value());
            if(record.prefab->source_type != AssetType::Prefab)
                writer.field("type", to_string(record.prefab->source_type));
            writer.key("base");
            ChildrenIndex base_children;
            const auto& base = record.prefab->base;
            for(std::size_t index = 0; index < base.size(); ++index)
                base_children[base[index].parent.value_or(INVALID_ENTITY_UUID)].push_back(index);
            const auto& roots = base_children[INVALID_ENTITY_UUID];
            if(roots.size() != 1)
                return Result<void>::failure("Prefab baseline must have exactly one root");
            if(auto written =
                    write_expanded_tree(writer, base[roots.front()], base, base_children, 1);
                !written)
                return written;
            writer.key("entities");
            writer.begin_array();
            std::map<EntityUuid, EntityUuid> mapping(
                record.prefab->entities.begin(), record.prefab->entities.end());
            for(const auto& [source, instance] : mapping) {
                writer.begin_object();
                writer.field("source", source.to_string());
                writer.field("instance", instance.to_string());
                writer.end_object();
            }
            writer.end_array();
            writer.end_object();
        }

        if(auto written = write_components(writer, record); !written)
            return written;
        if(const auto found = children.find(record.uuid); found != children.end()) {
            writer.key("children");
            writer.begin_array();
            for(const auto index : found->second) {
                if(auto result =
                        write_expanded_tree(writer, records[index], records, children, depth + 1);
                    !result)
                    return result;
            }
            writer.end_array();
        }
        writer.end_object();
        return Result<void>::success();
    }
    EntityRecord remap_record(EntityRecord record, const EntityTrees::ReferenceRemap& mapping) {
        const auto remap = [&](EntityUuid& id) {
            if(const auto found = mapping.find(id); found != mapping.end())
                id = found->second;
        };
        remap(record.uuid);
        if(record.parent)
            remap(*record.parent);
        for(auto& component : record.components)
            for(auto& property : component.properties)
                visit_entity_references(property.value, remap);
        if(record.prefab) {
            record.prefab = std::make_shared<PrefabRecord>(*record.prefab);
            for(auto& [source, target] : record.prefab->entities)
                remap(target);
        }
        return record;
    }

    bool same_record(const EntityRecord&, const EntityRecord&);
    bool same_prefab(
        const std::shared_ptr<PrefabRecord>& a, const std::shared_ptr<PrefabRecord>& b) {
        if(!a || !b)
            return !a && !b;
        return a->asset == b->asset && a->source_type == b->source_type
               && a->entities == b->entities && std::ranges::equal(a->base, b->base, same_record);
    }
    bool same_record(const EntityRecord& a, const EntityRecord& b) {
        if(a.uuid != b.uuid || a.parent != b.parent || a.name != b.name
            || a.components.size() != b.components.size() || !same_prefab(a.prefab, b.prefab))
            return false;
        for(size_t i = 0; i < a.components.size(); ++i) {
            const auto& left = a.components[i];
            const auto& right = b.components[i];
            if(left.descriptor != right.descriptor
                || left.properties.size() != right.properties.size())
                return false;
            for(size_t j = 0; j < left.properties.size(); ++j)
                if(left.properties[j].descriptor != right.properties[j].descriptor
                    || left.properties[j].value != right.properties[j].value)
                    return false;
        }
        return true;
    }

    Result<void> write_record(Json::Writer& writer, const EntityRecord& record) {
        writer.begin_object();
        writer.field("parent", record.parent.value_or(INVALID_ENTITY_UUID).to_string());
        writer.key("entity");
        if(auto written = write_expanded_tree(writer, record, {}, {}, 1); !written)
            return written;
        writer.end_object();
        return Result<void>::success();
    }

    Result<void> write_scene_tree(Json::Writer& writer, const EntityRecord& record,
        const std::vector<EntityRecord>& records, const ChildrenIndex& children, size_t depth,
        bool referenced, const std::unordered_set<EntityUuid>* excluded) {
        if(depth > SceneSerializer::MAX_HIERARCHY_DEPTH)
            return Result<void>::failure("Maximum scene hierarchy depth exceeded");
        writer.begin_object();
        writer.field("uuid", record.uuid.to_string());
        if(auto written = write_components(writer, record, !record.prefab); !written)
            return written;
        if(record.prefab) {
            const auto& prefab = *record.prefab;
            writer.key("prefab");
            writer.begin_object();
            writer.field("asset", prefab.asset.value());
            if(prefab.source_type != AssetType::Prefab)
                writer.field("type", to_string(prefab.source_type));
            if(!referenced) {
                writer.key("base");
                ChildrenIndex base_children;
                for(size_t i = 0; i < prefab.base.size(); ++i)
                    base_children[prefab.base[i].parent.value_or(INVALID_ENTITY_UUID)].push_back(i);
                const auto& roots = base_children[INVALID_ENTITY_UUID];
                if(roots.size() != 1)
                    return Result<void>::failure("Prefab baseline must have one root");
                if(auto written = write_expanded_tree(
                       writer, prefab.base[roots.front()], prefab.base, base_children, 1);
                    !written)
                    return written;
            }
            writer.key("entities");
            writer.begin_object();
            std::unordered_set<EntityUuid> members;
            for(const auto& [source, target] :
                std::map<EntityUuid, EntityUuid>(prefab.entities.begin(), prefab.entities.end())) {
                writer.field(source.to_string(), target.to_string());
                members.insert(target);
            }
            writer.end_object();
            std::unordered_map<EntityUuid, const EntityRecord*> actual;
            for(const auto& value : records)
                actual.emplace(value.uuid, &value);
            std::vector<EntityUuid> removed;
            writer.key("changes");
            writer.begin_array();
            for(const auto& before : prefab.base) {
                auto mapped = remap_record(before, prefab.entities);
                const auto found = actual.find(mapped.uuid);
                if(found == actual.end()) {
                    removed.push_back(before.uuid);
                    continue;
                }
                auto after = *found->second;
                if(after.uuid == record.uuid) {
                    after.parent.reset();
                    after.prefab.reset();
                }
                if(after.uuid != record.uuid && same_record(mapped, after))
                    continue;
                auto original = before;
                const bool inherit_prefab =
                    original.prefab && same_prefab(mapped.prefab, after.prefab);
                if(inherit_prefab) {
                    original.prefab.reset();
                    after.prefab.reset();
                }
                writer.begin_object();
                if(inherit_prefab)
                    writer.field("inherit_prefab", true);
                writer.key("base");
                if(auto written = write_record(writer, original); !written)
                    return written;
                writer.key("value");
                if(auto written = write_record(writer, after); !written)
                    return written;
                writer.end_object();
            }
            writer.end_array();
            writer.key("removed");
            writer.begin_array();
            for(const auto id : removed)
                writer.value(id.to_string());
            writer.end_array();
            // 局部新增子树保留原父级；嵌套实例继续用相同编码。
            std::unordered_set<EntityUuid> additions;
            std::vector<size_t> pending;
            if(const auto found = children.find(record.uuid); found != children.end())
                pending = found->second;
            for(size_t next = 0; next < pending.size(); ++next) {
                const auto& value = records[pending[next]];
                if(!members.contains(value.uuid))
                    additions.insert(value.uuid);
                if(const auto found = children.find(value.uuid); found != children.end())
                    pending.insert(pending.end(), found->second.begin(), found->second.end());
            }
            writer.key("added");
            writer.begin_array();
            for(const auto index : pending) {
                const auto& value = records[index];
                if(!additions.contains(value.uuid)
                    || (value.parent && additions.contains(*value.parent)))
                    continue;
                writer.begin_object();
                writer.field("parent", value.parent.value_or(INVALID_ENTITY_UUID).to_string());
                writer.key("entity");
                if(auto written = write_scene_tree(
                       writer, value, records, children, depth + 1, referenced, &members);
                    !written)
                    return written;
                writer.end_object();
            }
            writer.end_array();
            writer.end_object();
        } else if(const auto found = children.find(record.uuid); found != children.end()) {
            writer.key("children");
            writer.begin_array();
            for(const auto index : found->second) {
                if(excluded && excluded->contains(records[index].uuid))
                    continue;
                if(auto written = write_scene_tree(
                       writer, records[index], records, children, depth + 1, referenced, excluded);
                    !written)
                    return written;
            }
            writer.end_array();
        }
        writer.end_object();
        return Result<void>::success();
    }

}

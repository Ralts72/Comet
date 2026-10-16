#include "asset/runtime/prefab_loader.h"
#include "scene/prefab.h"
#include "scene/scene_runtime.h"
#include <limits>
#include "asset/data/model_data.h"
#include "asset/database.h"
#include "scene/scene_serializer.h"
#include "scene/script_component.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"
#include <gtest/gtest.h>

namespace Comet::Tests {
    TEST(PrefabTest, NestedBlockSampleLoadsThroughTheProjectIndex) {
        const auto registry = create_scene_component_registry();
        AssetDatabase database(ProjectPaths(std::filesystem::path(PROJECT_ROOT_DIR) / "demo"));
        ASSERT_TRUE(database.scan().succeeded());
        const auto* record = database.find("prefabs/block_pair.prefab");
        ASSERT_TRUE(record);
        const auto prefab = read_prefab_content(database, record->handle, registry);
        ASSERT_TRUE(prefab) << prefab.error();
        Scene scene;
        const auto placed =
            prefab.value().instantiate(scene, registry, std::nullopt, {}, record->handle);
        ASSERT_TRUE(placed);
        const auto children = scene.get_children(placed.value());
        ASSERT_EQ(children.size(), 2u);
        for(const auto child : children)
            EXPECT_TRUE(child.has_component<PrefabInstanceComponent>());
        EXPECT_NE(children[0].get_uuid(), children[1].get_uuid());
    }

    TEST(PrefabTest, NestedSourcesKeepOverridesAndStableReferencesAcrossResolveCloneAndPlacement) {
        const auto registry = create_scene_component_registry();
        const AssetHandle inner_handle{710}, outer_handle{711};
        Scene inner_source;
        auto inner = inner_source.create_entity("Inner");
        auto leaf = inner_source.create_entity("Leaf");
        ASSERT_TRUE(inner_source.set_parent(leaf, inner));
        leaf.add_component<ScriptComponent>().parameters = {{"owner", inner.get_uuid()}};
        const auto first_inner = Prefab::capture(inner_source, registry, inner);
        ASSERT_TRUE(first_inner);
        Scene assembly;
        const auto assembly_root = assembly.create_entity("Assembly");
        const auto placed_inner = first_inner.value().instantiate(
            assembly, registry, Math::Vec3(3, 0, 0), assembly_root.get_uuid(), inner_handle);
        ASSERT_TRUE(placed_inner);
        auto local_leaf = assembly.get_children(placed_inner.value()).front();
        local_leaf.edit_transform([](auto& value) { value.translation.y = 7; });
        ASSERT_TRUE(first_inner.value().instantiate(
            assembly, registry, std::nullopt, placed_inner.value().get_uuid(), inner_handle));
        const auto outer = Prefab::capture(assembly, registry, assembly_root);
        ASSERT_TRUE(outer) << outer.error();
        const auto text = outer.value().serialize(registry);
        ASSERT_TRUE(text) << text.error();
        const auto restored = Prefab::deserialize(text.value(), registry);
        ASSERT_TRUE(restored) << restored.error();
        leaf.get_component<NameComponent>().name = "Updated leaf";
        leaf.edit_transform([](auto& value) { value.translation.y = 2; });
        auto added = inner_source.create_entity("Added");
        ASSERT_TRUE(inner_source.set_parent(added, inner));
        const auto updated_inner = Prefab::capture(inner_source, registry, inner);
        ASSERT_TRUE(updated_inner);
        const auto load = [&](AssetHandle handle) -> Result<Prefab> {
            if(handle != inner_handle)
                return Result<Prefab>::failure("Unexpected nested source");
            return updated_inner;
        };
        const auto resolved = restored.value().resolve_nested(registry, outer_handle, load);
        const auto repeated = restored.value().resolve_nested(registry, outer_handle, load);
        ASSERT_TRUE(resolved) << resolved.error();
        ASSERT_TRUE(repeated) << repeated.error();
        EXPECT_EQ(resolved.value().serialize(registry).value(),
            repeated.value().serialize(registry).value());
        Scene level;
        const auto a =
            resolved.value().instantiate(level, registry, Math::Vec3(10, 0, 0), {}, outer_handle);
        const auto b =
            resolved.value().instantiate(level, registry, Math::Vec3(20, 0, 0), {}, outer_handle);
        ASSERT_TRUE(a);
        ASSERT_TRUE(b);
        const auto a_inner = level.get_children(a.value()).front();
        const auto b_inner = level.get_children(b.value()).front();
        ASSERT_TRUE(a_inner.has_component<PrefabInstanceComponent>());
        ASSERT_TRUE(b_inner.has_component<PrefabInstanceComponent>());
        const auto& link = a_inner.get_component<PrefabInstanceComponent>();
        size_t local_nested = 0;
        for(const auto child : level.get_children(a_inner))
            if(child.has_component<PrefabInstanceComponent>()) {
                ++local_nested;
                EXPECT_TRUE(child.get_component<PrefabInstanceComponent>().entities.contains(
                    added.get_uuid()));
            }
        EXPECT_EQ(local_nested, 1u);
        EXPECT_EQ(link.asset, inner_handle);
        EXPECT_EQ(a_inner.get_component<TransformComponent>().translation, Math::Vec3(3, 0, 0));
        const auto changed = level.find_entity(link.entities.at(leaf.get_uuid()));
        EXPECT_EQ(changed.get_component<NameComponent>().name, "Updated leaf");
        EXPECT_EQ(changed.get_component<TransformComponent>().translation.y, 7);
        EXPECT_EQ(
            std::get<EntityUuid>(changed.get_component<ScriptComponent>().parameters.at("owner")),
            a_inner.get_uuid());
        EXPECT_NE(link.entities.at(added.get_uuid()),
            b_inner.get_component<PrefabInstanceComponent>().entities.at(added.get_uuid()));
        const SceneSerializer serializer(registry);
        const auto clone = serializer.clone(level);
        ASSERT_TRUE(clone) << clone.error();
        const auto saved = serializer.serialize(level);
        ASSERT_TRUE(saved) << saved.error();
        const auto reopened = serializer.deserialize(saved.value());
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value()
                      ->find_entity(a_inner.get_uuid())
                      .get_component<PrefabInstanceComponent>()
                      .entities,
            link.entities);
        EXPECT_EQ(clone.value()->entity_count(), level.entity_count());
        Scene playing;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.start(playing));
        const auto requested =
            playing.request_instantiate_prefab(std::make_shared<const Prefab>(resolved.value()),
                std::make_shared<const ComponentRegistry>(registry));
        ASSERT_TRUE(requested);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(playing.entity_count(), resolved.value().entity_count());
        for(const auto entity : playing.get_entities())
            EXPECT_FALSE(entity.has_component<PrefabInstanceComponent>());
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PrefabTest, NestedDependencyCyclesFailWithoutChangingTheStoredContent) {
        const auto registry = create_scene_component_registry();
        const AssetHandle a_handle{810}, b_handle{811};
        Scene plain;
        const auto plain_root = plain.create_entity("Plain");
        const auto empty = Prefab::capture(plain, registry, plain_root);
        ASSERT_TRUE(empty);
        Scene a_scene, b_scene;
        const auto a_root = a_scene.create_entity("A");
        const auto b_root = b_scene.create_entity("B");
        ASSERT_TRUE(empty.value().instantiate(
            a_scene, registry, std::nullopt, a_root.get_uuid(), b_handle));
        ASSERT_TRUE(empty.value().instantiate(
            b_scene, registry, std::nullopt, b_root.get_uuid(), a_handle));
        const auto a = Prefab::capture(a_scene, registry, a_root);
        const auto b = Prefab::capture(b_scene, registry, b_root);
        ASSERT_TRUE(a);
        ASSERT_TRUE(b);
        const auto before = a.value().serialize(registry);
        const auto failed = a.value().resolve_nested(
            registry, a_handle, [&](AssetHandle handle) { return handle == a_handle ? a : b; });
        EXPECT_FALSE(failed);
        EXPECT_EQ(a.value().serialize(registry).value(), before.value());
        EXPECT_FALSE(a.value().resolve_nested(registry, a_handle,
            [](AssetHandle) { return Result<Prefab>::failure("Missing child source"); }));
    }

    TEST(PrefabTest, RuntimeRequestsShareInstanceRemappingAndDiscardPendingWorkOnStop) {
        const auto components =
            std::make_shared<ComponentRegistry>(create_scene_component_registry());
        Scene source;
        auto root = source.create_entity("Door");
        auto child = source.create_entity("Switch");
        ASSERT_TRUE(source.set_parent(child, root));
        child.add_component<ScriptComponent>().parameters = {{"owner", root.get_uuid()}};
        root.add_component<MeshRendererComponent>(
            MeshRendererComponent{.mesh = AssetHandle{11}, .material = AssetHandle{22}});
        auto captured = Prefab::capture(source, *components, root);
        ASSERT_TRUE(captured) << captured.error();
        auto prefab = std::make_shared<Prefab>(std::move(captured).value());
        Scene scene;
        EXPECT_FALSE(scene.request_instantiate_prefab(prefab, components));
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(scene.request_instantiate_prefab(
            prefab, components, Math::Vec3(std::numeric_limits<float>::quiet_NaN())));
        const auto first =
            scene.request_instantiate_prefab(prefab, components, Math::Vec3(2, 3, 4));
        const auto second = scene.request_instantiate_prefab(prefab, components);
        ASSERT_TRUE(first);
        ASSERT_TRUE(second);
        EXPECT_NE(first, second);
        EXPECT_EQ(scene.entity_count(), 0);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(scene.entity_count(), 4);
        for(const auto id : {*first, *second}) {
            const auto instance = scene.find_entity(id);
            ASSERT_TRUE(instance);
            EXPECT_FALSE(instance.has_component<PrefabInstanceComponent>());
            const auto children = scene.get_children(instance);
            ASSERT_EQ(children.size(), 1);
            EXPECT_EQ(std::get<EntityUuid>(
                          children[0].get_component<ScriptComponent>().parameters.at("owner")),
                id);
            EXPECT_EQ(instance.get_component<MeshRendererComponent>().mesh, AssetHandle{11});
        }
        EXPECT_EQ(scene.find_entity(*first).get_component<TransformComponent>().translation,
            Math::Vec3(2, 3, 4));
        const auto cancelled = scene.request_instantiate_prefab(prefab, components);
        ASSERT_TRUE(cancelled);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FALSE(scene.find_entity(*cancelled));
        // 两节点子树与普通创建共用总量预算，不只计队列长度。
        for(int i = 0; i < 512; ++i)
            ASSERT_TRUE(scene.request_instantiate_prefab(prefab, components));
        EXPECT_FALSE(scene.request_create_entity());
        EXPECT_FALSE(scene.request_instantiate_prefab(prefab, components));
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(scene.entity_count(), 4);
    }

    TEST(PrefabTest, DemoCharacterPickupAndGateReuseContentForEditorPlacementAndRuntimeSpawn) {
        const auto components =
            std::make_shared<ComponentRegistry>(create_scene_component_registry());
        const std::filesystem::path assets =
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets";
        for(const auto& [file, handle] :
            {std::pair{"character.prefab", AssetHandle{7863069821640201101ULL}},
                std::pair{"pickup.prefab", AssetHandle{7863069821640201102ULL}},
                std::pair{"finish_gate.prefab", AssetHandle{7863069821640201103ULL}}}) {
            SCOPED_TRACE(file);
            auto loaded = Prefab::load(assets / "prefabs" / file, *components);
            ASSERT_TRUE(loaded) << loaded.error();
            auto prefab = std::make_shared<Prefab>(std::move(loaded).value());
            Scene placed;
            auto editor =
                prefab->instantiate(placed, *components, Math::Vec3(10, 1, 0), {}, handle);
            ASSERT_TRUE(editor) << editor.error();
            EXPECT_TRUE(editor.value().has_component<PrefabInstanceComponent>());
            const auto references = components->collect_asset_references(
                placed, ComponentRegistry::ReferenceScope::Runtime);
            Scene playing;
            SceneRuntime runtime;
            ASSERT_TRUE(runtime.start(playing));
            const auto spawned =
                playing.request_instantiate_prefab(prefab, components, Math::Vec3(-10, 1, 0));
            ASSERT_TRUE(spawned);
            EXPECT_FALSE(playing.find_entity(*spawned));
            ASSERT_TRUE(runtime.advance(0));
            auto root = playing.find_entity(*spawned);
            ASSERT_TRUE(root);
            EXPECT_NE(root.get_uuid(), editor.value().get_uuid());
            EXPECT_FALSE(root.has_component<PrefabInstanceComponent>());
            EXPECT_EQ(playing.entity_count(), placed.entity_count());
            EXPECT_EQ(root.get_component<TransformComponent>().translation, Math::Vec3(-10, 1, 0));
            EXPECT_EQ(components->collect_asset_references(
                          playing, ComponentRegistry::ReferenceScope::Runtime),
                references);
            // Capture rejects references outside the subtree, including references to the source or another instance.
            EXPECT_TRUE(Prefab::capture(playing, *components, root));
            const auto update = prefab->update_candidate(placed, *components, editor.value());
            ASSERT_TRUE(update) << update.error();
            EXPECT_EQ(update.value().front().uuid, editor.value().get_uuid());
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST(PrefabTest, ModelSourcesKeepNodeIdentityAcrossReorderAndHierarchyChanges) {
        const auto registry = create_scene_component_registry();
        const AssetHandle source{817};
        ModelData model;
        model.nodes = {{.key = "node/name/4:Body", .name = "Body"},
            {.key = "node/name/4:Door",
                .name = "Door",
                .parent = 0,
                .parts = {{AssetHandle{11}, AssetHandle{12}}}},
            {.key = "node/name/3:Old", .name = "Old", .parent = 0}};
        model.scenes = {{.name = "Main", .roots = {0}}};
        auto original = Prefab::from_model(model, source, registry, "House");
        ASSERT_TRUE(original) << original.error();
        auto again = Prefab::from_model(model, source, registry, "House");
        ASSERT_TRUE(again);
        EXPECT_EQ(original.value().serialize(registry).value(),
            again.value().serialize(registry).value());
        auto different = Prefab::from_model(model, AssetHandle{818}, registry, "House");
        ASSERT_TRUE(different);
        EXPECT_NE(original.value().root_uuid(), different.value().root_uuid());
        Scene scene;
        auto placed = original.value().instantiate(
            scene, registry, Math::Vec3(7, 0, 0), {}, source, AssetType::Model);
        ASSERT_TRUE(placed);
        const auto root = placed.value();
        const auto body = scene.get_children(root).front();
        const auto find_named = [](Scene& scene, std::string_view name) {
            for(auto entity : scene.get_entities())
                if(entity.get_component<NameComponent>().name == name)
                    return entity;
            return Entity{};
        };
        const auto door = find_named(scene, "Door");
        const auto old = find_named(scene, "Old");
        ASSERT_TRUE(door);
        ASSERT_TRUE(old);
        const auto primitive = scene.get_children(door).front();
        primitive.edit_component<MeshRendererComponent>(
            [](auto& value) { value.material = AssetHandle{99}; });
        door.edit_transform([](auto& value) { value.scale = {2, 2, 2}; });
        auto door_node = model.nodes[1];
        door_node.parent = ModelData::NO_PARENT;
        door_node.transform.translation = {1, 2, 3};
        door_node.transform.scale = {4, 4, 4};
        door_node.parts[0] = {AssetHandle{21}, AssetHandle{22}};
        model.nodes = {
            door_node, model.nodes[0], {.key = "node/name/3:New", .name = "New", .parent = 0}};
        model.scenes[0].roots = {1, 0};
        auto incoming = Prefab::from_model(model, source, registry, "Renamed file");
        ASSERT_TRUE(incoming) << incoming.error();
        EXPECT_EQ(incoming.value().root_uuid(), original.value().root_uuid());
        auto update = incoming.value().update_candidate(scene, registry, root);
        ASSERT_TRUE(update) << update.error();
        Scene merged;
        ASSERT_TRUE(EntityTrees::restore(merged, registry, update.value()));
        auto merged_root = merged.find_entity(root.get_uuid());
        auto merged_door = merged.find_entity(door.get_uuid());
        ASSERT_TRUE(merged_door);
        EXPECT_TRUE(merged.find_entity(body.get_uuid()));
        EXPECT_FALSE(merged.find_entity(old.get_uuid()));
        EXPECT_EQ(merged.get_parent(merged_door), merged_root);
        EXPECT_EQ(merged.get_parent(find_named(merged, "New")), merged_door);
        EXPECT_EQ(merged_root.get_component<TransformComponent>().translation, Math::Vec3(7, 0, 0));
        EXPECT_EQ(merged_door.get_component<TransformComponent>().translation, Math::Vec3(1, 2, 3));
        EXPECT_EQ(merged_door.get_component<TransformComponent>().scale, Math::Vec3(2));
        const auto& mesh =
            merged.find_entity(primitive.get_uuid()).get_component<MeshRendererComponent>();
        EXPECT_EQ(mesh.mesh, AssetHandle{21});
        EXPECT_EQ(mesh.material, AssetHandle{99});
        EXPECT_FALSE(incoming.value().prepare_source_update(merged, registry, merged_root));

        const SceneSerializer serializer(registry);
        auto text = serializer.serialize(merged);
        ASSERT_TRUE(text) << text.error();
        auto reopened = serializer.deserialize(text.value());
        auto cloned = serializer.clone(merged);
        ASSERT_TRUE(reopened) << reopened.error();
        ASSERT_TRUE(cloned) << cloned.error();
        for(auto* level : {reopened.value().get(), cloned.value().get()}) {
            const auto restored = level->find_entity(root.get_uuid());
            const auto& link = restored.get_component<PrefabInstanceComponent>();
            EXPECT_EQ(link.source_type, AssetType::Model);
            EXPECT_EQ(link.asset, source);
            EXPECT_EQ(link.entities, merged_root.get_component<PrefabInstanceComponent>().entities);
            const auto refs = registry.collect_asset_references(*level);
            EXPECT_TRUE(std::ranges::any_of(refs, [&](const auto& ref) {
                return ref.handle == source && ref.type == AssetType::Model;
            }));
            const auto runtime = registry.collect_asset_references(
                *level, ComponentRegistry::ReferenceScope::Runtime);
            EXPECT_TRUE(std::ranges::none_of(
                runtime, [&](const auto& ref) { return ref.handle == source; }));
            auto reverted = incoming.value().update_candidate(
                *level, registry, restored, Prefab::UpdateMode::RevertOverrides);
            ASSERT_TRUE(reverted) << reverted.error();
            Scene clean;
            ASSERT_TRUE(EntityTrees::restore(clean, registry, reverted.value()));
            EXPECT_EQ(clean.find_entity(door.get_uuid()).get_component<TransformComponent>().scale,
                Math::Vec3(4));
            EXPECT_EQ(clean.find_entity(primitive.get_uuid())
                          .get_component<MeshRendererComponent>()
                          .material,
                AssetHandle{22});
            EXPECT_EQ(clean.find_entity(root.get_uuid())
                          .get_component<PrefabInstanceComponent>()
                          .source_type,
                AssetType::Model);
        }
        auto bad_text = text.value();
        const auto type_pos = bad_text.find("\"model\"");
        ASSERT_NE(type_pos, std::string::npos);
        bad_text.replace(type_pos, 7, "\"mesh\"");
        EXPECT_FALSE(serializer.deserialize(bad_text));
    }

    TEST(PrefabTest, ModelSourceRejectsMissingOrDuplicateNodeKeys) {
        const auto registry = create_scene_component_registry();
        ModelData model;
        model.nodes = {{.key = "stable", .name = "Node"}};
        model.scenes = {{.roots = {0}}};
        EXPECT_FALSE(Prefab::from_model(model, {}, registry, "Model"));
        model.nodes[0].key.clear();
        EXPECT_FALSE(Prefab::from_model(model, AssetHandle{17}, registry, "Model"));
        model.nodes[0].key = "stable";
        model.nodes.push_back(model.nodes[0]);
        model.scenes[0].roots.push_back(1);
        EXPECT_FALSE(Prefab::from_model(model, AssetHandle{17}, registry, "Model"));
        // 普通程序化放置无需导入器来源键。
        Scene scene;
        EXPECT_TRUE(scene.instantiate_model(model, "Unlinked", {}));
    }

    TEST(PrefabTest, CapturesOnlyTheSubtreeAndRemapsJointAndScriptReferencesPerInstance) {
        auto registry = create_scene_component_registry();
        Scene source;
        auto outside = source.create_entity("Level parent");
        auto root = source.create_entity("Bridge");
        auto child = source.create_entity("Platform");
        ASSERT_TRUE(source.set_parent(root, outside));
        ASSERT_TRUE(source.set_parent(child, root));
        root.edit_transform([](auto& value) { value.translation = {1, 2, 3}; });
        child.edit_transform([](auto& value) { value.translation = {0, 4, 0}; });
        root.add_component<PointJointComponent>(
            PointJointComponent{.connected_body = child.get_uuid()});
        auto& script = root.add_component<ScriptComponent>();
        script.parameters["target"] = child.get_uuid();
        script.parameters["self"] = root.get_uuid();
        auto captured = Prefab::capture(source, registry, root);
        ASSERT_TRUE(captured) << captured.error();
        EXPECT_EQ(captured.value().entity_count(), 2);
        const auto source_root = root.get_uuid();
        source.destroy_entity(outside);

        const TemporaryDirectory directory;
        const auto path = directory.path() / "bridge.prefab";
        ASSERT_TRUE(captured.value().save(path, registry));
        auto text = read_text_file(path);
        ASSERT_TRUE(text);
        EXPECT_EQ(text.value().find("environment"), std::string::npos);
        EXPECT_EQ(text.value().find("post_process"), std::string::npos);
        auto prefab = Prefab::load(path, registry);
        ASSERT_TRUE(prefab) << prefab.error();
        EXPECT_EQ(prefab.value().root_uuid(), source_root);
        Scene level;
        auto parent = level.create_entity("Placement parent");
        auto a =
            prefab.value().instantiate(level, registry, Math::Vec3(5, 0, 0), parent.get_uuid());
        auto b = prefab.value().instantiate(level, registry, Math::Vec3(10, 0, 0));
        ASSERT_TRUE(a) << a.error();
        ASSERT_TRUE(b) << b.error();
        EXPECT_NE(a.value().get_uuid(), b.value().get_uuid());
        EXPECT_NE(a.value().get_uuid(), source_root);
        EXPECT_EQ(level.get_parent(a.value()), parent);
        EXPECT_FALSE(level.get_parent(b.value()));
        for(const auto root_entity : {a.value(), b.value()}) {
            const auto children = level.get_children(root_entity);
            ASSERT_EQ(children.size(), 1);
            EXPECT_EQ(root_entity.get_component<PointJointComponent>().connected_body,
                children[0].get_uuid());
            const auto& parameters = root_entity.get_component<ScriptComponent>().parameters;
            EXPECT_EQ(std::get<EntityUuid>(parameters.at("target")), children[0].get_uuid());
            EXPECT_EQ(std::get<EntityUuid>(parameters.at("self")), root_entity.get_uuid());
            EXPECT_EQ(
                children[0].get_component<TransformComponent>().translation, Math::Vec3(0, 4, 0));
        }
        EXPECT_EQ(a.value().get_component<TransformComponent>().translation, Math::Vec3(5, 0, 0));
        EXPECT_EQ(b.value().get_component<TransformComponent>().translation, Math::Vec3(10, 0, 0));
        const auto content = SceneSerializer(registry).serialize(level);
        ASSERT_TRUE(content);
        const auto reopened = SceneSerializer(registry).deserialize(content.value());
        ASSERT_TRUE(reopened);
        EXPECT_EQ(reopened.value()->entity_count(), 5);
        const auto& restored = reopened.value()->find_entity(a.value().get_uuid());
        EXPECT_EQ(restored.get_component<PointJointComponent>().connected_body,
            level.get_children(a.value())[0].get_uuid());
    }

    TEST(PrefabTest, LinkedInstancesKeepSourceIdentityAcrossSaveCloneAndSubtreeDuplication) {
        const auto registry = create_scene_component_registry();
        Scene source;
        const auto source_root = source.create_entity("Tree");
        auto child = source.create_entity("Branch");
        ASSERT_TRUE(source.set_parent(child, source_root));
        auto prefab = Prefab::capture(source, registry, source_root);
        ASSERT_TRUE(prefab);
        Scene level;
        const AssetHandle asset{731};
        auto placed = prefab.value().instantiate(level, registry, Math::Vec3(5, 0, 0), {}, asset);
        ASSERT_TRUE(placed);
        auto root = placed.value();
        ASSERT_TRUE(root.has_component<PrefabInstanceComponent>());
        const auto mapping = root.get_component<PrefabInstanceComponent>().entities;
        EXPECT_EQ(mapping.at(source_root.get_uuid()), root.get_uuid());
        EXPECT_EQ(mapping.at(child.get_uuid()), level.get_children(root)[0].get_uuid());
        root.get_component<NameComponent>().name = "Local name";
        const SceneSerializer serializer(registry);
        auto text = serializer.serialize(level);
        ASSERT_TRUE(text) << text.error();
        auto reopened = serializer.deserialize(text.value());
        ASSERT_TRUE(reopened) << reopened.error();
        auto cloned = serializer.clone(level);
        ASSERT_TRUE(cloned) << cloned.error();
        for(auto* scene : {reopened.value().get(), cloned.value().get()}) {
            const auto restored = scene->find_entity(root.get_uuid());
            const auto& link = restored.get_component<PrefabInstanceComponent>();
            EXPECT_EQ(link.asset, asset);
            EXPECT_EQ(link.entities, mapping);
            EXPECT_EQ(link.base->root_uuid(), source_root.get_uuid());
            EXPECT_EQ(restored.get_component<NameComponent>().name, "Local name");
            Scene baseline;
            auto original = link.base->instantiate(baseline, registry);
            ASSERT_TRUE(original);
            EXPECT_EQ(original.value().get_component<NameComponent>().name, "Tree");
        }
        const auto all = registry.collect_asset_references(level);
        EXPECT_TRUE(std::ranges::any_of(
            all, [&](const auto& reference) { return reference.handle == asset; }));
        const auto runtime =
            registry.collect_asset_references(level, ComponentRegistry::ReferenceScope::Runtime);
        EXPECT_TRUE(std::ranges::none_of(
            runtime, [&](const auto& reference) { return reference.handle == asset; }));

        // 本地删除保留源身份，但复制后的缺失节点不能指回原实例。
        level.destroy_entity(level.find_entity(mapping.at(child.get_uuid())));
        auto snapshot = EntityTrees::capture(level, registry, root);
        ASSERT_TRUE(snapshot);
        auto remap = EntityTrees::make_instance_ids(*snapshot, level);
        ASSERT_TRUE(remap);
        ASSERT_TRUE(EntityTrees::restore(level, registry, *snapshot, *remap));
        const auto copy = level.find_entity(snapshot->front().uuid);
        const auto& copied = copy.get_component<PrefabInstanceComponent>();
        EXPECT_EQ(copied.entities.at(source_root.get_uuid()), copy.get_uuid());
        EXPECT_NE(copied.entities.at(child.get_uuid()), mapping.at(child.get_uuid()));
        EXPECT_FALSE(level.find_entity(copied.entities.at(child.get_uuid())));
        text = serializer.serialize(level);
        ASSERT_TRUE(text) << text.error();
        EXPECT_TRUE(serializer.deserialize(text.value()));
        // 另存 Prefab 展开已有来源，结果为可独立复用的纯内容。
        auto flattened = Prefab::capture(level, registry, root);
        ASSERT_TRUE(flattened) << flattened.error();
        auto flat_text = flattened.value().serialize(registry);
        ASSERT_TRUE(flat_text);
        EXPECT_EQ(flat_text.value().find("\"prefab\""), std::string::npos);
        EXPECT_TRUE(Prefab::deserialize(flat_text.value(), registry));

        auto& invalid = root.get_component<PrefabInstanceComponent>();
        invalid.entities[source_root.get_uuid()] = copy.get_uuid();
        EXPECT_FALSE(serializer.serialize(level));
    }

    TEST(PrefabTest, SourceUpdateKeepsInstanceIdentityPlacementAndLocalPropertyOverrides) {
        const auto registry = create_scene_component_registry();
        Scene source;
        auto root = source.create_entity("Source");
        auto child = source.create_entity("Child");
        auto removed = source.create_entity("Removed");
        ASSERT_TRUE(source.set_parent(child, root));
        ASSERT_TRUE(source.set_parent(removed, root));
        auto& parameters = child.add_component<ScriptComponent>().parameters;
        parameters["damage"] = 10.0f;
        parameters["range"] = 2.0f;
        auto original = Prefab::capture(source, registry, root);
        ASSERT_TRUE(original);
        Scene level;
        auto parent = level.create_entity("Level");
        auto placed = original.value().instantiate(
            level, registry, Math::Vec3(7, 0, 0), parent.get_uuid(), AssetHandle{51});
        ASSERT_TRUE(placed);
        const auto instance = placed.value();
        const auto mapping = instance.get_component<PrefabInstanceComponent>().entities;
        const auto child_id = mapping.at(child.get_uuid());
        const auto removed_id = mapping.at(removed.get_uuid());
        auto local_child = level.find_entity(child_id);
        local_child.get_component<NameComponent>().name = "Local child";
        local_child.edit_transform([](auto& value) { value.scale = {2, 2, 2}; });
        local_child.get_component<ScriptComponent>().parameters["damage"] = 99.0f;
        child.edit_transform([](auto& value) {
            value.translation = {0, 3, 0};
            value.scale = {4, 4, 4};
        });
        parameters["damage"] = 20.0f;
        parameters["range"] = 5.0f;
        parameters["new"] = true;
        root.edit_transform([](auto& value) { value.translation = {100, 0, 0}; });
        root.get_component<NameComponent>().name = "New source name";
        source.destroy_entity(removed);
        auto added = source.create_entity("New child");
        ASSERT_TRUE(source.set_parent(added, child));
        added.add_component<PointJointComponent>(
            PointJointComponent{.connected_body = root.get_uuid()});
        auto updated = Prefab::capture(source, registry, root);
        ASSERT_TRUE(updated);
        const SceneSerializer serializer(registry);
        const auto before = serializer.serialize(level);
        ASSERT_TRUE(before);
        auto candidate = updated.value().update_candidate(level, registry, instance);
        ASSERT_TRUE(candidate) << candidate.error();
        EXPECT_EQ(serializer.serialize(level).value(), before.value());
        Scene merged;
        ASSERT_TRUE(merged.create_entity_with_uuid(parent.get_uuid(), "Level"));
        ASSERT_TRUE(EntityTrees::restore(merged, registry, candidate.value()));
        auto merged_root = merged.find_entity(instance.get_uuid());
        auto merged_child = merged.find_entity(child_id);
        EXPECT_EQ(merged.get_parent(merged_root).get_uuid(), parent.get_uuid());
        EXPECT_EQ(merged_root.get_component<TransformComponent>().translation, Math::Vec3(7, 0, 0));
        EXPECT_EQ(merged_root.get_component<NameComponent>().name, "New source name");
        EXPECT_EQ(merged_child.get_component<NameComponent>().name, "Local child");
        EXPECT_EQ(
            merged_child.get_component<TransformComponent>().translation, Math::Vec3(0, 3, 0));
        EXPECT_EQ(merged_child.get_component<TransformComponent>().scale, Math::Vec3(2, 2, 2));
        const auto& merged_parameters = merged_child.get_component<ScriptComponent>().parameters;
        EXPECT_EQ(std::get<float>(merged_parameters.at("damage")), 99);
        EXPECT_EQ(std::get<float>(merged_parameters.at("range")), 5);
        EXPECT_TRUE(std::get<bool>(merged_parameters.at("new")));
        EXPECT_FALSE(merged.find_entity(removed_id));
        const auto& link = merged_root.get_component<PrefabInstanceComponent>();
        const auto added_id = link.entities.at(added.get_uuid());
        EXPECT_EQ(merged.get_parent(merged.find_entity(added_id)), merged_child);
        EXPECT_EQ(merged.find_entity(added_id).get_component<PointJointComponent>().connected_body,
            instance.get_uuid());
        EXPECT_EQ(link.entities.at(child.get_uuid()), child_id);
        child.edit_transform([](auto& value) { value.translation.y = 8; });
        updated = Prefab::capture(source, registry, root);
        ASSERT_TRUE(updated);
        auto second = updated.value().update_candidate(merged, registry, merged_root);
        ASSERT_TRUE(second) << second.error();
        Scene twice;
        ASSERT_TRUE(twice.create_entity_with_uuid(parent.get_uuid(), "Level"));
        ASSERT_TRUE(EntityTrees::restore(twice, registry, second.value()));
        EXPECT_EQ(twice.find_entity(child_id).get_component<TransformComponent>().translation.y, 8);
        EXPECT_EQ(twice.find_entity(instance.get_uuid())
                      .get_component<PrefabInstanceComponent>()
                      .entities.at(added.get_uuid()),
            added_id);
    }

    TEST(PrefabTest, DestructiveSourceUpdatesReportConflictsWithoutMutatingTheInstance) {
        const auto registry = create_scene_component_registry();
        Scene source;
        auto root = source.create_entity();
        auto child = source.create_entity("Child");
        ASSERT_TRUE(source.set_parent(child, root));
        auto original = Prefab::capture(source, registry, root);
        ASSERT_TRUE(original);
        Scene level;
        auto placed = original.value().instantiate(level, registry, {}, {}, AssetHandle{18});
        ASSERT_TRUE(placed);
        auto instance = placed.value();
        auto local = level.get_children(instance).front();
        local.get_component<NameComponent>().name = "Local override";
        source.destroy_entity(child);
        auto updated = Prefab::capture(source, registry, root);
        ASSERT_TRUE(updated);
        const SceneSerializer serializer(registry);
        const auto before = serializer.serialize(level).value();
        EXPECT_FALSE(updated.value().update_candidate(level, registry, instance));
        EXPECT_EQ(serializer.serialize(level).value(), before);
        local.get_component<NameComponent>().name = "Child";
        auto outside = level.create_entity("Other level entity");
        outside.add_component<ScriptComponent>().parameters["target"] = local.get_uuid();
        EXPECT_FALSE(updated.value().update_candidate(level, registry, instance));
        EXPECT_TRUE(level.find_entity(local.get_uuid()));
        level.destroy_entity(outside);
        EXPECT_TRUE(updated.value().update_candidate(level, registry, instance));
    }

    TEST(PrefabTest, RejectsExternalReferencesAndInvalidContentWithoutChangingTheScene) {
        auto registry = create_scene_component_registry();
        Scene source;
        auto root = source.create_entity();
        auto outside = source.create_entity();
        root.add_component<PointJointComponent>(
            PointJointComponent{.connected_body = outside.get_uuid()});
        EXPECT_FALSE(Prefab::capture(source, registry, root));
        root.edit_component<PointJointComponent>([](auto& joint) { joint.connected_body = {}; });
        root.add_component<ScriptComponent>().parameters["external"] = outside.get_uuid();
        EXPECT_FALSE(Prefab::capture(source, registry, root));
        root.get_component<ScriptComponent>().parameters.clear();
        auto prefab = Prefab::capture(source, registry, root);
        ASSERT_TRUE(prefab);
        Scene target;
        auto keep = target.create_entity("Keep");
        EXPECT_FALSE(
            prefab.value().instantiate(target, registry, std::nullopt, outside.get_uuid()));
        EXPECT_EQ(target.entity_count(), 1);
        EXPECT_EQ(keep.get_component<NameComponent>().name, "Keep");
        EXPECT_FALSE(Prefab::deserialize(R"({"version":2,"entities":[]})", registry));
        const auto scene_text = SceneSerializer(registry).serialize(source);
        ASSERT_TRUE(scene_text);
        EXPECT_FALSE(Prefab::deserialize(scene_text.value(), registry));
        EXPECT_FALSE(Prefab::deserialize("invalid", registry));
        EXPECT_EQ(source.entity_count(), 2);
    }
}

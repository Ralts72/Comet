#include "support/blocked_worker.h"
#include "asset/default_assets.h"
#include "render/resource/environment.h"
#include "assets/editor_assets.h"
#include "assets/material_editing.h"
#include "asset/serialization/metadata_serializer.h"
#include "asset/artifact/mesh_artifact.h"
#include "asset/artifact/model_artifact.h"
#include "asset/registry.h"
#include "core/task_scheduler.h"
#include "core/project.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"
#include "render/material/material.h"
#include "scene/scene_document.h"
#include "scene/scene_editor.h"
#include "scene/selection.h"
#include "editor_state.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"
#include "scene/scene_runtime.h"
#include "scene/script_component.h"
#include "scene/systems/script_system.h"
#include "asset/script.h"
#include "common/file_io.h"
#include "common/scope_exit.h"
#include "diagnostics/logger.h"
#include "asset/serialization/material_serializer.h"
#include "support/temporary_directory.h"
#include "support/render_resource_factory.h"
#include "support/hdr_image.h"

#include <gtest/gtest.h>
#include <spdlog/sinks/callback_sink.h>
#include <algorithm>
#include <chrono>
#include <array>
#include <fstream>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

namespace CometEditor::Tests {
    class EditorAssetsTest: public ::testing::Test {
    protected:
        Comet::Tests::FakeRenderResourceFactory factory;
        Comet::Tests::TemporaryDirectory directory;
        const std::filesystem::path root = directory.path();
        Comet::AssetRegistry runtime;
        Comet::TaskScheduler scheduler{1};
        std::unique_ptr<EditorAssets> assets;
        Comet::AssetHandle mesh;
        bool reject_trash = false;
        bool reject_metadata_trash = false;
        int trash_requests = 0;
        std::vector<std::filesystem::path> trash_sources;
        void SetUp() override {
            factory.fail_texture_creation(true);
            const auto directory = Comet::ProjectPaths(root).assets();
            std::filesystem::create_directories(directory);
            std::filesystem::copy_file(
                std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/meshes/cube.gltf",
                directory / "model.gltf");
            ASSERT_TRUE(Comet::MetadataSerializer{}.save(
                Comet::AssetMetadata{
                    .handle = Comet::AssetHandle::generate(), .type = Comet::AssetType::Mesh},
                Comet::metadata_path(directory / "model.gltf")));
            assets = std::make_unique<EditorAssets>(Comet::ProjectPaths(root), runtime, factory,
                scheduler, DEFAULT_FILE_CHANGE_QUIET_PERIOD, Comet::AssetImportLimits{},
                [this](const std::filesystem::path& entry) -> Comet::Result<void> {
                    ++trash_requests;
                    trash_sources.push_back(entry);
                    if(reject_trash || (reject_metadata_trash && entry.extension() == ".meta"))
                        return Comet::Result<void>::failure("System trash is unavailable");
                    const auto destination = root / "fake-system-trash" / entry.filename();
                    std::error_code error;
                    std::filesystem::create_directories(destination.parent_path(), error);
                    if(!error)
                        std::filesystem::rename(entry, destination, error);
                    if(error)
                        return Comet::Result<void>::failure(error.message());
                    return Comet::Result<void>::success();
                });
            ASSERT_TRUE(assets->refresh().succeeded());
            mesh = assets->database().find("model.gltf")->handle;
        }
        void TearDown() override {
            assets.reset();
            runtime.clear();
        }
        std::filesystem::path artifact_path() const {
            return Comet::ProjectPaths(root).cache() / "imported/mesh"
                   / (std::to_string(mesh.value()) + ".bin");
        }
        void require_external_mesh_buffer() const {
            std::ofstream(Comet::ProjectPaths(root).assets() / "model.gltf", std::ios::trunc)
                << R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42,"uri":"mesh.bin"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}]})";
        }
        void add_external_mesh_buffer() const {
            const std::array<float, 9> vertices{0, 0, 0, 1, 0, 0, 0, 1, 0};
            const std::array<std::uint16_t, 3> indices{0, 1, 2};
            std::ofstream buffer(Comet::ProjectPaths(root).assets() / "mesh.bin", std::ios::binary);
            buffer.write(reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
            buffer.write(reinterpret_cast<const char*>(indices.data()), sizeof(indices));
        }
        void complete_imports() {
            // 发布软预算可能让一次更新只消费一个结果，需分轮取完。
            const auto count = assets->database().get_assets().size();
            for(std::size_t i = 0; i <= count; ++i) {
                ASSERT_TRUE(assets->update());
                scheduler.wait_idle();
            }
            ASSERT_TRUE(assets->update());
        }

        std::optional<Comet::AssetScanReport> wait_for_source_report() {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while(std::chrono::steady_clock::now() < deadline) {
                auto result = assets->update();
                if(!result) {
                    ADD_FAILURE() << result.error().message;
                    return std::nullopt;
                }
                if(result.value())
                    return std::move(result).value();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return std::nullopt;
        }

        Comet::AssetHandle add_material() {
            std::ofstream(Comet::ProjectPaths(root).assets() / "placement.mat")
                << R"({"version": 2, "template": "test", "properties": {}})";
            EXPECT_TRUE(assets->refresh().succeeded());
            const auto* record = assets->database().find("placement.mat");
            if(!record)
                return {};
            EXPECT_TRUE(runtime.register_asset(
                record->handle, std::make_shared<Comet::Material>("Placement", "test")));
            return record->handle;
        }
    };

    TEST_F(EditorAssetsTest, CollisionOnlyScenePreparationAndRestoreDoNotUploadAMesh) {
        using namespace Comet;
        complete_imports();
        Scene scene;
        const auto components = create_scene_component_registry();
        auto entity = scene.create_entity("Collision only");
        entity.add_component<RigidBodyComponent>(RigidBodyComponent{.motion = BodyMotion::Static});
        entity.add_component<ColliderComponent>(
            ColliderComponent{.shape = ColliderShape::TriangleMesh, .mesh = mesh});
        auto prepared = assets->prepare_scene(scene, components);
        ASSERT_TRUE(prepared) << prepared.error().message;
        EXPECT_EQ(prepared.value(), 0u);
        assets->track_scene(scene, components);
        ASSERT_TRUE(assets->restore_references());
        EXPECT_EQ(factory.mesh_creation_count(), 0u);
        EXPECT_FALSE(runtime.contains(mesh));
        auto geometry = assets->read_mesh_data(mesh);
        ASSERT_TRUE(geometry) << geometry.error().message;
        EXPECT_FALSE(geometry.value().indices.empty());
        entity.add_component<MeshRendererComponent>(
            MeshRendererComponent{mesh, DEFAULT_PBR_MATERIAL});
        assets->track_scene(scene, components);
        ASSERT_TRUE(assets->restore_references());
        EXPECT_EQ(factory.mesh_creation_count(), 1u);
        EXPECT_TRUE(runtime.contains(mesh));
    }

    class PrefabEditingTest: public EditorAssetsTest {
    protected:
        using Type = SceneEditor::StructureRequest::Type;
        Comet::Scene scene;
        Comet::ComponentRegistry components = Comet::create_scene_component_registry();
        CommandHistory history;
        PropertyEditTransaction edit{history, components};
        SelectionService selection{scene};
        EditorState state;
        std::unique_ptr<SceneEditor> editor;
        Comet::AssetHandle prefab;
        Comet::EntityUuid source_root, source_child, first, second;
        std::filesystem::path path;
        void SetUp() override {
            EditorAssetsTest::SetUp();
            history.bind_scene(&scene);
            editor =
                std::make_unique<SceneEditor>(state, history, edit, components, selection, *assets);
            auto root = scene.create_entity("Platform");
            auto child = scene.create_entity("Anchor");
            ASSERT_TRUE(scene.set_parent(child, root));
            source_root = root.get_uuid();
            source_child = child.get_uuid();
            path = Comet::ProjectPaths(EditorAssetsTest::root).assets() / "platform.prefab";
            ASSERT_TRUE(editor->save_prefab(&scene, source_root, path, history.generation()));
            prefab = assets->database().find("platform.prefab")->handle;
            const SceneEditor::AssetInput input{{prefab, assets->database().get_revision(prefab)},
                history.generation(), Comet::AssetType::Prefab};
            ASSERT_TRUE(editor->place_asset(&scene, input, {3, 0, 0}));
            first = selection.get_selected_entity().get_uuid();
            ASSERT_TRUE(editor->place_asset(&scene, input, {9, 0, 0}));
            second = selection.get_selected_entity().get_uuid();
        }
        bool operate(Type type, Comet::EntityUuid entity) {
            return editor->execute(&scene, {type, entity, {}, history.generation()});
        }
        Comet::EntityUuid child_of(Comet::EntityUuid root) {
            return scene.find_entity(root)
                .get_component<Comet::PrefabInstanceComponent>()
                .entities.at(source_child);
        }
        std::string contents() { return Comet::read_text_file(path).value(); }
        std::string scene_contents() {
            return Comet::SceneSerializer(components).serialize(scene).value();
        }
    };

    TEST_F(PrefabEditingTest,
        MultiSelectionSurvivesPrefabUpdatesAndBatchPlacementUsesOneHistoryEntry) {
        selection.select_entities(std::vector{first, second});
        ASSERT_TRUE(operate(Type::UpdatePrefab, first));
        EXPECT_EQ(selection.get_selected_entities(), (std::vector{first, second}));
        ASSERT_TRUE(editor->undo(&scene));
        EXPECT_EQ(selection.get_selected_entities(), (std::vector{first, second}));
        const auto before = scene.entity_count();
        const auto cursor = history.undo_size();
        SceneEditor::StructureRequest request{Type::Duplicate, second, {}, history.generation(),
            selection.get_selected_entities(), {.copies = 2, .world_step = {0, 0, 3}}};
        selection.clear();
        ASSERT_TRUE(editor->execute(&scene, request));
        const auto placed = selection.get_selected_entities();
        ASSERT_EQ(placed.size(), 4u);
        EXPECT_EQ(scene.entity_count(), before + 8);
        EXPECT_EQ(history.undo_size(), cursor + 1);
        std::unordered_set<Comet::EntityUuid> children;
        for(const auto uuid : placed) {
            const auto entity = scene.find_entity(uuid);
            ASSERT_TRUE(entity.has_component<Comet::PrefabInstanceComponent>());
            const auto& link = entity.get_component<Comet::PrefabInstanceComponent>();
            EXPECT_EQ(link.asset, prefab);
            EXPECT_EQ(link.entities.at(source_root), uuid);
            EXPECT_TRUE(children.insert(link.entities.at(source_child)).second);
            EXPECT_EQ(scene.get_parent(scene.find_entity(link.entities.at(source_child))), entity);
        }
        ASSERT_TRUE(editor->copy_entities(&scene, placed, history.generation()));
        ASSERT_TRUE(editor->undo(&scene));
        EXPECT_EQ(scene.entity_count(), before);
        EXPECT_TRUE(selection.get_selected_entities().empty());
        ASSERT_TRUE(editor->redo(&scene));
        for(const auto uuid : placed)
            EXPECT_TRUE(scene.find_entity(uuid));
        request.type = Type::Delete;
        request.entities = placed;
        state.mode = EditorMode::Play;
        EXPECT_FALSE(editor->execute(&scene, request));
        state.mode = EditorMode::Edit;
        ASSERT_TRUE(editor->execute(&scene, request));
        EXPECT_EQ(scene.entity_count(), before);
        ASSERT_TRUE(editor->undo(&scene));
        EXPECT_EQ(scene.entity_count(), before + 8);
        ++request.generation;
        EXPECT_FALSE(editor->execute(&scene, request));
    }

    TEST_F(PrefabEditingTest, ApplyAndUpdateOtherInstanceUndoBothSourceAndInstanceState) {
        const auto before = contents();
        const auto metadata = Comet::read_text_file(Comet::metadata_path(path)).value();
        const auto first_child = child_of(first), second_child = child_of(second);
        ASSERT_TRUE(
            edit.apply({first_child, "transform", "translation"}, Comet::Math::Vec3(0, 4, 0)));
        ASSERT_TRUE(edit.apply({second_child, "transform", "scale"}, Comet::Math::Vec3(2, 2, 2)));
        const auto extra = SceneCommands::create_entity(history, components, "New anchor", first);
        ASSERT_TRUE(extra);
        scene.find_entity(extra).add_component<Comet::PointJointComponent>(
            Comet::PointJointComponent{.connected_body = first_child});
        selection.select_entity(scene.find_entity(first).get_id());
        const auto scene_before = scene_contents();
        const auto undo_before = history.undo_size();
        ASSERT_TRUE(operate(Type::ApplyPrefab, first));
        EXPECT_EQ(history.undo_size(), undo_before + 1);
        EXPECT_EQ(selection.get_selected_entity().get_uuid(), first);
        const auto written = contents();
        EXPECT_NE(written, before);
        EXPECT_EQ(Comet::read_text_file(Comet::metadata_path(path)).value(), metadata);
        auto source =
            assets->load_prefab({prefab, assets->database().get_revision(prefab)}, components);
        ASSERT_TRUE(source);
        EXPECT_EQ(source.value().root_uuid(), source_root);
        Comet::Scene fresh;
        auto instantiated = source.value().instantiate(fresh, components);
        ASSERT_TRUE(instantiated);
        EXPECT_EQ(instantiated.value().get_component<Comet::TransformComponent>().translation,
            Comet::Math::Vec3(0));
        const auto first_link =
            scene.find_entity(first).get_component<Comet::PrefabInstanceComponent>();
        EXPECT_EQ(first_link.entities.at(source_child), first_child);
        Comet::EntityUuid extra_source;
        for(const auto& [source, target] : first_link.entities)
            if(target == extra)
                extra_source = source;
        ASSERT_TRUE(extra_source);
        ASSERT_TRUE(operate(Type::UpdatePrefab, second));
        const auto second_extra =
            scene.find_entity(second).get_component<Comet::PrefabInstanceComponent>().entities.at(
                extra_source);
        EXPECT_NE(second_extra, extra);
        EXPECT_EQ(scene.find_entity(second_extra)
                      .get_component<Comet::PointJointComponent>()
                      .connected_body,
            second_child);
        EXPECT_EQ(scene.find_entity(second_child)
                      .get_component<Comet::TransformComponent>()
                      .translation.y,
            4);
        EXPECT_EQ(scene.find_entity(second_child).get_component<Comet::TransformComponent>().scale,
            Comet::Math::Vec3(2));
        ASSERT_TRUE(editor->undo(&scene));
        EXPECT_EQ(contents(), written);
        ASSERT_TRUE(editor->undo(&scene));
        EXPECT_EQ(contents(), before);
        EXPECT_EQ(scene_contents(), scene_before);
        ASSERT_TRUE(editor->redo(&scene));
        EXPECT_EQ(contents(), written);
        ASSERT_TRUE(editor->redo(&scene));
        EXPECT_TRUE(scene.find_entity(second_extra));
        EXPECT_EQ(
            scene.find_entity(first).get_component<Comet::PrefabInstanceComponent>().entities.at(
                extra_source),
            extra);
        auto roundtrip = Comet::SceneSerializer(components).deserialize(scene_contents());
        ASSERT_TRUE(roundtrip);
        EXPECT_TRUE(roundtrip.value()->find_entity(extra));
        EXPECT_TRUE(roundtrip.value()->find_entity(second_extra));
    }

    TEST_F(PrefabEditingTest, SaveFailureRestoresFileSceneSelectionAndUndoCursor) {
        const auto before = contents();
        ASSERT_TRUE(
            edit.apply({child_of(first), "transform", "translation"}, Comet::Math::Vec3(0, 8, 0)));
        selection.select_entity(scene.find_entity(first).get_id());
        const auto scene_before = scene_contents();
        const auto undo_before = history.undo_size();
        const auto revision = assets->database().get_revision(prefab);
        const auto directory = path.parent_path();
        std::ofstream(directory / "broken.mat")
            << R"({"version":2,"template":"pbr","properties":{}})";
        std::ofstream(directory / "broken.mat.meta") << "invalid metadata";
        EXPECT_FALSE(operate(Type::ApplyPrefab, first));
        EXPECT_EQ(contents(), before);
        EXPECT_EQ(scene_contents(), scene_before);
        EXPECT_EQ(history.undo_size(), undo_before);
        EXPECT_EQ(selection.get_selected_entity().get_uuid(), first);
        EXPECT_EQ(assets->database().get_revision(prefab), revision);
        std::filesystem::remove(directory / "broken.mat");
        std::filesystem::remove(directory / "broken.mat.meta");
        state.mode = EditorMode::Play;
        EXPECT_FALSE(operate(Type::ApplyPrefab, first));
        EXPECT_FALSE(operate(Type::RevertPrefab, first));
        state.mode = EditorMode::Edit;
        ASSERT_TRUE(operate(Type::ApplyPrefab, first));
        EXPECT_NE(contents(), before);
        // 其他资产编辑更改了来源时，旧撤销不能覆盖新的源内容。
        auto different = Comet::Prefab::capture(scene, components, scene.find_entity(source_root));
        ASSERT_TRUE(different);
        ASSERT_TRUE(assets->save_prefab(
            {prefab, assets->database().get_revision(prefab)}, different.value(), components));
        const auto changed_source = contents();
        const auto changed_scene = scene_contents();
        EXPECT_FALSE(editor->undo(&scene));
        EXPECT_EQ(contents(), changed_source);
        EXPECT_EQ(scene_contents(), changed_scene);
    }

    TEST_F(PrefabEditingTest, RevertRestoresDeletedMembersAndRejectsDanglingLevelReferences) {
        const auto before = contents();
        const auto original_child = child_of(first);
        ASSERT_TRUE(SceneCommands::delete_entity(history, components, original_child));
        const auto added = SceneCommands::create_entity(history, components, "Extra", first);
        ASSERT_TRUE(added);
        auto outside = scene.create_entity("Outside");
        outside.add_component<Comet::ScriptComponent>().parameters["target"] = added;
        EXPECT_FALSE(operate(Type::RevertPrefab, first));
        EXPECT_TRUE(scene.find_entity(added));
        EXPECT_FALSE(scene.find_entity(original_child));
        scene.destroy_entity(outside);
        const auto modified = scene_contents();
        ASSERT_TRUE(operate(Type::RevertPrefab, first));
        EXPECT_FALSE(scene.find_entity(added));
        EXPECT_TRUE(scene.find_entity(original_child));
        EXPECT_EQ(scene.find_entity(first).get_component<Comet::TransformComponent>().translation,
            Comet::Math::Vec3(3, 0, 0));
        EXPECT_EQ(contents(), before);
        ASSERT_TRUE(editor->undo(&scene));
        EXPECT_EQ(scene_contents(), modified);
        ASSERT_TRUE(editor->redo(&scene));
        EXPECT_TRUE(scene.find_entity(original_child));
        EXPECT_EQ(contents(), before);
    }

    TEST_F(EditorAssetsTest, PrefabSaveAndPlacementShareProjectIdentityAndSceneHistory) {
        Comet::Scene scene;
        auto parent = scene.create_entity("Outside");
        auto source = scene.create_entity("Platform");
        auto child = scene.create_entity("Anchor");
        ASSERT_TRUE(scene.set_parent(source, parent));
        ASSERT_TRUE(scene.set_parent(child, source));
        source.add_component<Comet::PointJointComponent>(
            Comet::PointJointComponent{.connected_body = child.get_uuid()});
        child.edit_transform([](auto& transform) { transform.translation = {0, 2, 0}; });
        auto components = Comet::create_scene_component_registry();
        CommandHistory history;
        history.bind_scene(&scene);
        PropertyEditTransaction edit(history, components);
        SelectionService selection(scene);
        EditorState state;
        SceneEditor editor(state, history, edit, components, selection, *assets);
        const auto generation = history.generation();
        const auto path = Comet::ProjectPaths(root).assets() / "platform.prefab";
        ASSERT_TRUE(editor.save_prefab(&scene, source.get_uuid(), path, generation));
        const auto* record = assets->database().find("platform.prefab");
        ASSERT_NE(record, nullptr);
        EXPECT_EQ(record->type, Comet::AssetType::Prefab);
        const auto handle = record->handle;
        const SceneEditor::AssetInput input{{handle, assets->database().get_revision(handle)},
            generation, Comet::AssetType::Prefab};
        EXPECT_TRUE(std::filesystem::exists(Comet::metadata_path(path)));
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_EQ(scene.get_parent(source), parent);
        EXPECT_FALSE(editor.save_prefab(&scene, source.get_uuid(), path, generation));
        EXPECT_FALSE(
            editor.save_prefab(&scene, source.get_uuid(), root / "outside.prefab", generation));
        EXPECT_FALSE(std::filesystem::exists(root / "outside.prefab"));
        EXPECT_FALSE(editor.save_prefab(&scene, source.get_uuid(), "stale.prefab", generation + 1));
        EXPECT_FALSE(assets->database().find("stale.prefab"));
        auto stale = input;
        ++stale.target.revision;
        EXPECT_FALSE(editor.place_asset(&scene, stale, {}));
        state.mode = EditorMode::Play;
        EXPECT_FALSE(editor.place_asset(&scene, input, {}));
        EXPECT_FALSE(editor.save_prefab(&scene, source.get_uuid(), "play.prefab", generation));
        EXPECT_FALSE(assets->database().find("play.prefab"));
        state.mode = EditorMode::Edit;
        ASSERT_TRUE(editor.place_asset(&scene, input, {3, 0, 4}));
        const auto instance = selection.get_selected_entity();
        const auto uuid = instance.get_uuid();
        EXPECT_NE(uuid, source.get_uuid());
        EXPECT_FALSE(scene.get_parent(instance));
        EXPECT_EQ(instance.get_component<Comet::TransformComponent>().translation,
            Comet::Math::Vec3(3, 0, 4));
        const auto children = scene.get_children(instance);
        ASSERT_EQ(children.size(), 1);
        const auto anchor = children.front().get_uuid();
        EXPECT_NE(anchor, child.get_uuid());
        EXPECT_EQ(instance.get_component<Comet::PointJointComponent>().connected_body, anchor);
        EXPECT_EQ(history.undo_size(), 1);
        ASSERT_TRUE(editor.undo(&scene));
        EXPECT_FALSE(scene.find_entity(uuid));
        EXPECT_EQ(scene.entity_count(), 3);
        ASSERT_TRUE(editor.redo(&scene));
        EXPECT_EQ(
            scene.find_entity(uuid).get_component<Comet::PointJointComponent>().connected_body,
            anchor);
        Comet::SceneSerializer serializer(components);
        auto serialized = serializer.serialize(scene);
        ASSERT_TRUE(serialized);
        auto reopened = serializer.deserialize(serialized.value());
        ASSERT_TRUE(reopened);
        EXPECT_EQ(reopened.value()
                      ->find_entity(uuid)
                      .get_component<Comet::PointJointComponent>()
                      .connected_body,
            anchor);
        EXPECT_TRUE(reopened.value()->find_entity(anchor));
        EXPECT_EQ(assets->database().find("platform.prefab")->handle, handle);
        child.edit_transform([](auto& value) { value.translation.y = 5; });
        auto updated = Comet::Prefab::capture(scene, components, source);
        ASSERT_TRUE(updated);
        ASSERT_TRUE(updated.value().save(path, components));
        ASSERT_TRUE(assets->refresh().succeeded());
        selection.select_entity(scene.find_entity(uuid).get_id());
        SceneEditor::StructureRequest update{
            SceneEditor::StructureRequest::Type::UpdatePrefab, uuid, {}, history.generation()};
        state.mode = EditorMode::Play;
        EXPECT_FALSE(editor.execute(&scene, update));
        state.mode = EditorMode::Edit;
        ASSERT_TRUE(editor.execute(&scene, update));
        EXPECT_EQ(selection.get_selected_entity().get_uuid(), uuid);
        EXPECT_EQ(
            scene.find_entity(anchor).get_component<Comet::TransformComponent>().translation.y, 5);
        ASSERT_TRUE(editor.undo(&scene));
        EXPECT_EQ(selection.get_selected_entity().get_uuid(), uuid);
        EXPECT_EQ(
            scene.find_entity(anchor).get_component<Comet::TransformComponent>().translation.y, 2);
        ASSERT_TRUE(editor.redo(&scene));
        EXPECT_EQ(selection.get_selected_entity().get_uuid(), uuid);
        EXPECT_EQ(
            scene.find_entity(anchor).get_component<Comet::TransformComponent>().translation.y, 5);
        const auto entity_count = scene.entity_count();
        ASSERT_TRUE(editor.execute(&scene,
            {SceneEditor::StructureRequest::Type::DetachPrefab, uuid, {}, history.generation()}));
        EXPECT_FALSE(scene.find_entity(uuid).has_component<Comet::PrefabInstanceComponent>());
        EXPECT_EQ(scene.entity_count(), entity_count);
        ASSERT_TRUE(editor.undo(&scene));
        ASSERT_TRUE(scene.find_entity(uuid).has_component<Comet::PrefabInstanceComponent>());
        EXPECT_EQ(
            scene.find_entity(uuid).get_component<Comet::PrefabInstanceComponent>().asset, handle);
        ASSERT_TRUE(editor.redo(&scene));
        EXPECT_FALSE(scene.find_entity(uuid).has_component<Comet::PrefabInstanceComponent>());
    }

    TEST_F(EditorAssetsTest, ModelReimportUpdatesLinkedInstancesThroughSaveAndUndo) {
        require_external_mesh_buffer();
        add_external_mesh_buffer();
        const auto directory = Comet::ProjectPaths(root).assets();
        const auto source = directory / "linked.gltf";
        auto mesh_text = Comet::read_text_file(directory / "model.gltf");
        ASSERT_TRUE(mesh_text);
        mesh_text.value().pop_back();
        const auto write_model = [&](int height) {
            return Comet::write_text_file_atomic(source,
                mesh_text.value() + R"(,"nodes":[{"name":"Body","mesh":0,"translation":[0,)"
                    + std::to_string(height) + R"(,0]}],"scenes":[{"nodes":[0]}],"scene":0})");
        };
        ASSERT_TRUE(write_model(1));
        ASSERT_TRUE(assets->refresh().succeeded());
        complete_imports();
        const auto handle = assets->database().find("linked.gltf")->handle;
        Comet::Scene scene;
        auto components = Comet::create_scene_component_registry();
        CommandHistory history;
        history.bind_scene(&scene);
        PropertyEditTransaction edit(history, components);
        SelectionService selection(scene);
        EditorState state;
        SceneEditor editor(state, history, edit, components, selection, *assets);
        const SceneEditor::AssetInput input{{handle, assets->database().get_revision(handle)},
            history.generation(), Comet::AssetType::Model};
        ASSERT_TRUE(editor.place_asset(&scene, input, {2, 0, 3}));
        const auto first = selection.get_selected_entity().get_uuid();
        ASSERT_TRUE(editor.place_asset(&scene, input, {-2, 0, 3}));
        const auto second = selection.get_selected_entity().get_uuid();
        const auto first_child = scene.get_children(scene.find_entity(first)).front().get_uuid();
        const auto second_child = scene.get_children(scene.find_entity(second)).front().get_uuid();
        scene.find_entity(first_child).edit_transform([](auto& t) { t.scale = {2, 2, 2}; });
        const Comet::SceneSerializer serializer(components);
        auto saved = serializer.serialize(scene);
        ASSERT_TRUE(saved);
        auto reopened = serializer.deserialize(saved.value());
        ASSERT_TRUE(reopened);
        editor.bind_scene(*reopened.value(), EditorMode::Edit);
        auto* level = reopened.value().get();
        const auto before = serializer.serialize(*level).value();
        ASSERT_TRUE(write_model(5));
        assets->request_source_reimport(handle);
        complete_imports();
        EXPECT_EQ(assets->database().find("linked.gltf")->handle, handle);
        const auto source_text = Comet::read_text_file(source).value();
        using Type = SceneEditor::StructureRequest::Type;
        EXPECT_FALSE(editor.execute(level, {Type::ApplyPrefab, first, {}, history.generation()}));
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_EQ(Comet::read_text_file(source).value(), source_text);
        EXPECT_EQ(serializer.serialize(*level).value(), before);
        ASSERT_TRUE(editor.execute(level, {Type::UpdatePrefab, first, {}, history.generation()}));
        EXPECT_EQ(level->find_entity(first_child)
                      .get_component<Comet::TransformComponent>()
                      .translation.y,
            5);
        EXPECT_EQ(level->find_entity(first_child).get_component<Comet::TransformComponent>().scale,
            Comet::Math::Vec3(2));
        EXPECT_EQ(level->find_entity(second_child)
                      .get_component<Comet::TransformComponent>()
                      .translation.y,
            1);
        EXPECT_EQ(level->find_entity(first).get_component<Comet::TransformComponent>().translation,
            Comet::Math::Vec3(2, 0, 3));
        ASSERT_TRUE(editor.undo(level));
        EXPECT_EQ(serializer.serialize(*level).value(), before);
        ASSERT_TRUE(editor.redo(level));
        ASSERT_TRUE(editor.execute(level, {Type::UpdatePrefab, second, {}, history.generation()}));
        EXPECT_EQ(level->find_entity(second_child)
                      .get_component<Comet::TransformComponent>()
                      .translation.y,
            5);
        ASSERT_TRUE(editor.execute(level, {Type::RevertPrefab, first, {}, history.generation()}));
        EXPECT_EQ(level->find_entity(first_child).get_component<Comet::TransformComponent>().scale,
            Comet::Math::Vec3(1));
        EXPECT_EQ(
            level->find_entity(first).get_component<Comet::PrefabInstanceComponent>().source_type,
            Comet::AssetType::Model);
        ASSERT_TRUE(editor.execute(level, {Type::DetachPrefab, first, {}, history.generation()}));
        EXPECT_FALSE(level->find_entity(first).has_component<Comet::PrefabInstanceComponent>());
        ASSERT_TRUE(editor.undo(level));
        EXPECT_EQ(
            level->find_entity(first).get_component<Comet::PrefabInstanceComponent>().source_type,
            Comet::AssetType::Model);
        EXPECT_EQ(Comet::read_text_file(source).value(), source_text);
        EXPECT_TRUE(assets->prepare_scene(*level, components));
    }

    TEST_F(EditorAssetsTest, ModelTextureThumbnailsFollowPublishedProductsWithoutGpuUploads) {
        const auto source = Comet::ProjectPaths(root).assets() / "truck.glb";
        std::filesystem::copy_file(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/models/milk_truck.glb",
            source);
        ASSERT_TRUE(assets->refresh().succeeded());
        complete_imports();
        const auto model = assets->database().find("truck.glb")->handle;
        Comet::AssetHandle texture;
        for(const auto& record : assets->database().get_assets())
            if(record.source_asset == model && record.type == Comet::AssetType::Texture) {
                texture = record.handle;
                break;
            }
        ASSERT_TRUE(texture);
        auto& thumbnails = assets->thumbnails();
        thumbnails.update();
        EXPECT_FALSE(thumbnails.request(texture).image);
        scheduler.wait_idle();
        thumbnails.update();
        auto first = thumbnails.request(texture);
        ASSERT_TRUE(first.image) << first.error;
        EXPECT_LE(first.image->width, AssetThumbnails::IMAGE_SIZE);
        EXPECT_LE(first.image->height, AssetThumbnails::IMAGE_SIZE);
        EXPECT_FALSE(runtime.contains(texture));
        assets->request_source_reimport(model);
        complete_imports();
        thumbnails.update();
        EXPECT_FALSE(thumbnails.request(texture).image);
        scheduler.wait_idle();
        thumbnails.update();
        auto next = thumbnails.request(texture);
        ASSERT_TRUE(next.image) << next.error;
        EXPECT_NE(next.image, first.image);
        EXPECT_EQ(next.image->pixels, first.image->pixels);
        EXPECT_FALSE(runtime.contains(texture));
    }

    TEST_F(EditorAssetsTest, ImportsModelAsOneBackgroundGroupAndIndexesChildrenBeforeLoading) {
        const auto source = Comet::ProjectPaths(root).assets() / "complete.gltf";
        std::filesystem::copy_file(source.parent_path() / "model.gltf", source);
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto model = assets->database().find("complete.gltf")->handle;
        EXPECT_EQ(assets->database().find(model)->type, Comet::AssetType::Model);
        {
            Comet::Tests::BlockedWorker blocked(scheduler);
            ASSERT_TRUE(assets->update());
            blocked.release();
            scheduler.wait_idle();
            const auto staged = Comet::MetadataSerializer{}.load(Comet::metadata_path(source));
            ASSERT_TRUE(staged);
            EXPECT_TRUE(staged.value().subresources.empty());
        }
        complete_imports();
        const auto published = Comet::MetadataSerializer{}.load(Comet::metadata_path(source));
        ASSERT_TRUE(published);
        ASSERT_FALSE(published.value().subresources.empty());
        for(const auto& child : published.value().subresources) {
            const auto* record = assets->database().find(child.handle);
            ASSERT_NE(record, nullptr);
            EXPECT_EQ(record->source_asset, model);
            EXPECT_TRUE(assets->load_reference(
                child.handle, child.type, assets->database().get_revision(child.handle)));
            EXPECT_FALSE(assets->move(child.handle, "moved.gltf").succeeded());
        }
        Comet::Scene scene;
        auto components = Comet::create_scene_component_registry();
        CommandHistory history;
        history.bind_scene(&scene);
        PropertyEditTransaction edit(history, components);
        SelectionService selection(scene);
        EditorState state;
        SceneEditor editor(state, history, edit, components, selection, *assets);
        const SceneEditor::AssetInput input{{model, assets->database().get_revision(model)},
            history.generation(), Comet::AssetType::Model};
        auto placed = editor.place_asset(&scene, input, {2, 0, 3});
        ASSERT_TRUE(placed) << placed.error().message;
        EXPECT_EQ(history.undo_size(), 1);
        EXPECT_GT(scene.entity_count(), 1);
        const auto selected = scene.find_entity(selection.get_selected_entity_id()).get_uuid();
        const auto count = scene.entity_count();
        auto stale = input;
        ++stale.target.revision;
        EXPECT_FALSE(editor.place_asset(&scene, stale, {}));
        state.mode = EditorMode::Play;
        EXPECT_FALSE(editor.place_asset(&scene, input, {}));
        EXPECT_EQ(scene.entity_count(), count);
        EXPECT_EQ(scene.find_entity(selection.get_selected_entity_id()).get_uuid(), selected);
        state.mode = EditorMode::Edit;
        ASSERT_TRUE(editor.undo(&scene));
        EXPECT_EQ(scene.entity_count(), 0);
        ASSERT_TRUE(editor.redo(&scene));
        EXPECT_TRUE(scene.find_entity(selected));
        EXPECT_EQ(scene.entity_count(), count);
    }

    TEST_F(EditorAssetsTest, SceneEditingRejectsStaleAndPlayRequestsWithoutChangingSelection) {
        Comet::Scene scene;
        auto components = Comet::create_scene_component_registry();
        CommandHistory history;
        history.bind_scene(&scene);
        PropertyEditTransaction edit(history, components);
        SelectionService selection(scene);
        EditorState state;
        SceneEditor editor(state, history, edit, components, selection, *assets);
        SceneEditor::StructureRequest request{
            SceneEditor::StructureRequest::Type::Create, {}, {}, history.generation()};
        ASSERT_TRUE(editor.execute(&scene, request));
        const auto selected = selection.get_selected_entity_id();
        EXPECT_EQ(scene.entity_count(), 1);
        ++request.generation;
        EXPECT_FALSE(editor.execute(&scene, request));
        EXPECT_EQ(selection.get_selected_entity_id(), selected);
        state.mode = EditorMode::Play;
        request.generation = history.generation();
        EXPECT_FALSE(editor.execute(&scene, request));
        EXPECT_FALSE(editor.undo(&scene));
        state.mode = EditorMode::Edit;
        ASSERT_TRUE(editor.undo(&scene));
        EXPECT_EQ(scene.entity_count(), 0);
        ASSERT_TRUE(editor.redo(&scene));
        EXPECT_EQ(scene.entity_count(), 1);
        Comet::Scene other;
        EXPECT_FALSE(editor.execute(&other, request));
    }

    TEST_F(EditorAssetsTest, SceneEditorCopiesIntoTheActiveDocumentOnly) {
        Comet::Scene source;
        auto original = source.create_entity("Original");
        auto components = Comet::create_scene_component_registry();
        CommandHistory history;
        history.bind_scene(&source);
        PropertyEditTransaction edit(history, components);
        SelectionService selection(source);
        EditorState state;
        SceneEditor editor(state, history, edit, components, selection, *assets);

        using Type = SceneEditor::StructureRequest::Type;
        ASSERT_TRUE(editor.copy_entity(&source, original.get_uuid(), history.generation()));
        EXPECT_TRUE(editor.clipboard().has_content());
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_FALSE(editor.copy_entity(&source, original.get_uuid(), history.generation() + 1));

        Comet::Scene destination;
        history.bind_scene(&destination);
        selection.set_scene(destination);
        const SceneEditor::StructureRequest paste{Type::Paste, {}, {}, history.generation()};
        EXPECT_FALSE(editor.execute(&source, paste));
        ASSERT_TRUE(editor.execute(&destination, paste));
        EXPECT_EQ(destination.entity_count(), 1);
        EXPECT_EQ(history.undo_size(), 1);
        EXPECT_EQ(destination.find_entity(selection.get_selected_entity_id())
                      .get_component<Comet::NameComponent>()
                      .name,
            "Original_Copy");
        state.mode = EditorMode::Play;
        EXPECT_FALSE(editor.execute(&destination, paste));
        EXPECT_EQ(destination.entity_count(), 1);
    }

    TEST_F(EditorAssetsTest, CopyingEntityDoesNotCommitActivePropertyEdit) {
        Comet::Scene scene;
        auto entity = scene.create_entity("Original");
        auto components = Comet::create_scene_component_registry();
        CommandHistory history;
        history.bind_scene(&scene);
        PropertyEditTransaction edit(history, components);
        SelectionService selection(scene);
        EditorState state;
        SceneEditor editor(state, history, edit, components, selection, *assets);

        ASSERT_TRUE(edit.begin({entity.get_uuid(), "name", "name"}));
        ASSERT_TRUE(edit.preview(std::string("Draft")));
        ASSERT_TRUE(editor.copy_entity(&scene, entity.get_uuid(), history.generation()));
        EXPECT_TRUE(edit.active());
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_TRUE(editor.clipboard().has_content());
    }

    TEST_F(EditorAssetsTest, DeletingAnotherEntityPreservesSelection) {
        Comet::Scene scene;
        auto selected = scene.create_entity("Selected");
        auto other = scene.create_entity("Other");
        auto components = Comet::create_scene_component_registry();
        CommandHistory history;
        history.bind_scene(&scene);
        PropertyEditTransaction edit(history, components);
        SelectionService selection(scene);
        EditorState state;
        SceneEditor editor(state, history, edit, components, selection, *assets);
        selection.select_entity(selected.get_id());

        using Type = SceneEditor::StructureRequest::Type;
        ASSERT_TRUE(
            editor.execute(&scene, {Type::Delete, other.get_uuid(), {}, history.generation()}));
        EXPECT_EQ(selection.get_selected_entity_id(), selected.get_id());
        ASSERT_TRUE(
            editor.execute(&scene, {Type::Delete, selected.get_uuid(), {}, history.generation()}));
        EXPECT_EQ(selection.get_selected_entity_id(), Comet::INVALID_ENTITY_ID);
    }

    TEST_F(EditorAssetsTest, EntityRenameUsesPropertyHistoryAndRejectsStaleRequests) {
        Comet::Scene scene;
        auto entity = scene.create_entity();
        const auto uuid = entity.get_uuid();
        auto components = Comet::create_scene_component_registry();
        CommandHistory history;
        history.bind_scene(&scene);
        PropertyEditTransaction edit(history, components);
        SelectionService selection(scene);
        EditorState state;
        SceneEditor editor(state, history, edit, components, selection, *assets);

        ASSERT_TRUE(editor.rename_entity(
            &scene, uuid, "新名称" + std::string(512, 'n'), history.generation()));
        EXPECT_EQ(history.undo_size(), 1);
        EXPECT_EQ(
            entity.get_component<Comet::NameComponent>().name, "新名称" + std::string(512, 'n'));
        ASSERT_TRUE(editor.undo(&scene));
        EXPECT_EQ(entity.get_component<Comet::NameComponent>().name, "Entity");
        ASSERT_TRUE(editor.redo(&scene));
        EXPECT_EQ(
            entity.get_component<Comet::NameComponent>().name, "新名称" + std::string(512, 'n'));
        EXPECT_FALSE(editor.rename_entity(&scene, uuid, "", history.generation()));
        EXPECT_FALSE(editor.rename_entity(&scene, uuid, "Stale", history.generation() + 1));
        state.mode = EditorMode::Play;
        EXPECT_FALSE(editor.rename_entity(&scene, uuid, "Runtime", history.generation()));
        state.mode = EditorMode::Edit;
        scene.destroy_entity(entity);
        EXPECT_FALSE(editor.rename_entity(&scene, uuid, "Missing", history.generation()));
        EXPECT_EQ(history.undo_size(), 1);
    }

    TEST_F(EditorAssetsTest, ScriptAssignmentResetsOverridesAsOneUndoableBindingChange) {
        const auto path = Comet::ProjectPaths(root).assets();
        ASSERT_TRUE(
            Comet::write_text_file_atomic(path / "a.lua", "return {properties = {speed = 1}}"));
        ASSERT_TRUE(
            Comet::write_text_file_atomic(path / "b.lua", "return {properties = {speed = false}}"));
        ASSERT_TRUE(Comet::write_text_file_atomic(path / "bad.lua", "return {"));
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto a = assets->database().find("a.lua")->handle;
        const auto b = assets->database().find("b.lua")->handle;
        const auto bad = assets->database().find("bad.lua")->handle;
        Comet::Scene scene;
        auto entity = scene.create_entity();
        auto& binding = entity.add_component<Comet::ScriptComponent>();
        auto components = Comet::create_scene_component_registry();
        CommandHistory history;
        history.bind_scene(&scene);
        PropertyEditTransaction edit(history, components);
        SelectionService selection(scene);
        EditorState state;
        SceneEditor editor(state, history, edit, components, selection, *assets);
        const PropertyEditTransaction::Target target{entity.get_uuid(), "script", "asset"};
        const auto input = [&](Comet::AssetHandle handle) {
            return SceneEditor::AssetInput{{handle, assets->database().get_revision(handle)},
                history.generation(), Comet::AssetType::Script};
        };
        ASSERT_TRUE(editor.assign_asset(&scene, target, input(a)));
        const Comet::ParameterMap overrides{{"speed", 20.0f}};
        ASSERT_TRUE(edit.apply({entity.get_uuid(), "script", "parameters"}, overrides));
        const auto before = history.state_id();
        const auto count = history.undo_size();
        ASSERT_TRUE(editor.assign_asset(&scene, target, input(b)));
        EXPECT_EQ(binding.asset, b);
        EXPECT_TRUE(binding.parameters.empty());
        EXPECT_EQ(history.undo_size(), count + 1);
        ASSERT_TRUE(editor.undo(&scene));
        EXPECT_EQ(binding.asset, a);
        EXPECT_EQ(binding.parameters, overrides);
        EXPECT_EQ(history.state_id(), before);
        EXPECT_FALSE(editor.assign_asset(&scene, target, input(bad)));
        EXPECT_EQ(binding.asset, a);
        EXPECT_EQ(binding.parameters, overrides);
        EXPECT_EQ(history.state_id(), before);
        ASSERT_TRUE(editor.redo(&scene));
        EXPECT_EQ(binding.asset, b);
        EXPECT_TRUE(binding.parameters.empty());
        const auto after = history.state_id();
        ASSERT_TRUE(editor.assign_asset(&scene, target, input(b)));
        EXPECT_EQ(history.state_id(), after);
        ASSERT_TRUE(editor.assign_asset(&scene, target, input({})));
        EXPECT_FALSE(binding.asset);
        EXPECT_TRUE(binding.parameters.empty());
        ASSERT_TRUE(editor.undo(&scene));
        EXPECT_EQ(binding.asset, b);
        EXPECT_EQ(history.state_id(), after);

        Comet::Scene playing;
        auto runtime_entity = playing.create_entity_with_uuid(entity.get_uuid());
        auto& runtime_binding = runtime_entity.add_component<Comet::ScriptComponent>();
        runtime_binding.asset = b;
        runtime_binding.parameters = {{"speed", true}};
        state.mode = EditorMode::Play;
        ASSERT_TRUE(editor.assign_asset(&playing, target, input(a)));
        EXPECT_TRUE(runtime_binding.parameters.empty());
        EXPECT_EQ(history.state_id(), after);
        EXPECT_EQ(binding.asset, b);
        Comet::SceneRuntime execution;
        ASSERT_TRUE(execution.add_system(std::make_unique<Comet::ScriptSystem>(runtime)));
        ASSERT_TRUE(execution.start(playing));
        ASSERT_TRUE(execution.stop());
    }

    TEST_F(
        EditorAssetsTest, DefaultCharacterCanBeAssignedUndoneAndUsedInPlayWithoutProjectMetadata) {
        const auto handle = Comet::DEFAULT_CHARACTER_MESH;
        EXPECT_FALSE(assets->database().find(handle));
        Comet::Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<Comet::MeshRendererComponent>(Comet::MeshRendererComponent{mesh, {}});
        auto components = Comet::create_scene_component_registry();
        CommandHistory history;
        history.bind_scene(&scene);
        PropertyEditTransaction edit(history, components);
        SelectionService selection(scene);
        EditorState state;
        SceneEditor editor(state, history, edit, components, selection, *assets);
        SceneEditor::AssetInput input{
            {handle, Comet::INVALID_ASSET_REVISION}, history.generation(), Comet::AssetType::Mesh};
        PropertyEditTransaction::Target target{entity.get_uuid(), "mesh_renderer", "mesh"};
        ASSERT_TRUE(editor.assign_asset(&scene, target, input));
        EXPECT_EQ(entity.get_component<Comet::MeshRendererComponent>().mesh, handle);
        EXPECT_TRUE(runtime.contains<Comet::Mesh>(handle));
        EXPECT_GT(factory.last_mesh_vertex_count(), 100u);
        ASSERT_TRUE(editor.undo(&scene));
        EXPECT_EQ(entity.get_component<Comet::MeshRendererComponent>().mesh, mesh);
        ASSERT_TRUE(editor.redo(&scene));
        EXPECT_EQ(entity.get_component<Comet::MeshRendererComponent>().mesh, handle);
        const auto history_state = history.state_id();
        Comet::Scene playing;
        auto copy = playing.create_entity_with_uuid(entity.get_uuid());
        copy.add_component<Comet::MeshRendererComponent>();
        state.mode = EditorMode::Play;
        ASSERT_TRUE(editor.assign_asset(&playing, target, input));
        EXPECT_EQ(copy.get_component<Comet::MeshRendererComponent>().mesh, handle);
        EXPECT_EQ(history.state_id(), history_state);
        EXPECT_FALSE(assets->load_reference(handle, Comet::AssetType::Material, 0));
    }

    TEST_F(EditorAssetsTest, DefaultMaterialAssignmentUsesExistingHistoryAndNeedsNoMetadata) {
        const auto material = add_material();
        Comet::Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<Comet::MeshRendererComponent>(
            Comet::MeshRendererComponent{mesh, material});
        auto components = Comet::create_scene_component_registry();
        CommandHistory history;
        history.bind_scene(&scene);
        PropertyEditTransaction edit(history, components);
        SelectionService selection(scene);
        EditorState state;
        SceneEditor editor(state, history, edit, components, selection, *assets);
        const PropertyEditTransaction::Target target{
            entity.get_uuid(), "mesh_renderer", "material"};
        for(const auto handle : {Comet::DEFAULT_PBR_MATERIAL, Comet::DEFAULT_UNLIT_MATERIAL}) {
            const SceneEditor::AssetInput input{
                {handle, 0}, history.generation(), Comet::AssetType::Material};
            ASSERT_TRUE(editor.assign_asset(&scene, target, input));
            EXPECT_EQ(entity.get_component<Comet::MeshRendererComponent>().material, handle);
            EXPECT_TRUE(runtime.contains<Comet::Material>(handle));
            EXPECT_FALSE(assets->database().find(handle));
            ASSERT_TRUE(editor.undo(&scene));
            EXPECT_EQ(entity.get_component<Comet::MeshRendererComponent>().material, material);
            EXPECT_FALSE(assets->load_reference(handle, Comet::AssetType::Material, 1));
            EXPECT_FALSE(assets->load_reference(handle, Comet::AssetType::Mesh, 0));
        }
    }

    TEST_F(EditorAssetsTest, BatchAssetAssignmentIsAtomicAndUndoRestoresDistinctValues) {
        const auto material = add_material();
        Comet::Scene scene;
        auto first = scene.create_entity();
        auto second = scene.create_entity();
        first.add_component<Comet::MeshRendererComponent>(mesh, Comet::DEFAULT_PBR_MATERIAL);
        second.add_component<Comet::MeshRendererComponent>(mesh, Comet::DEFAULT_UNLIT_MATERIAL);
        auto components = Comet::create_scene_component_registry();
        CommandHistory history;
        history.bind_scene(&scene);
        PropertyEditTransaction edit(history, components);
        SelectionService selection(scene);
        EditorState state;
        SceneEditor editor(state, history, edit, components, selection, *assets);
        SceneEditor::AssetInput input{{material, assets->database().get_revision(material)},
            history.generation(), Comet::AssetType::Material};
        std::array targets{
            PropertyEditTransaction::Target{first.get_uuid(), "mesh_renderer", "material"},
            PropertyEditTransaction::Target{second.get_uuid(), "mesh_renderer", "material"}};
        ASSERT_TRUE(editor.assign_assets(&scene, targets, input));
        EXPECT_EQ(first.get_component<Comet::MeshRendererComponent>().material, material);
        EXPECT_EQ(second.get_component<Comet::MeshRendererComponent>().material, material);
        ASSERT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(editor.undo(&scene));
        EXPECT_EQ(first.get_component<Comet::MeshRendererComponent>().material,
            Comet::DEFAULT_PBR_MATERIAL);
        EXPECT_EQ(second.get_component<Comet::MeshRendererComponent>().material,
            Comet::DEFAULT_UNLIT_MATERIAL);
        ASSERT_TRUE(editor.redo(&scene));
        ASSERT_TRUE(editor.undo(&scene));
        const auto cursor = history.state_id();
        ++input.generation;
        EXPECT_FALSE(editor.assign_assets(&scene, targets, input));
        --input.generation;
        auto missing = scene.create_entity();
        targets[1].entity = missing.get_uuid();
        EXPECT_FALSE(editor.assign_assets(&scene, targets, input));
        EXPECT_EQ(first.get_component<Comet::MeshRendererComponent>().material,
            Comet::DEFAULT_PBR_MATERIAL);
        EXPECT_EQ(history.state_id(), cursor);
        targets[1].entity = second.get_uuid();
        state.mode = EditorMode::Play;
        ASSERT_TRUE(editor.assign_assets(&scene, targets, input));
        EXPECT_EQ(first.get_component<Comet::MeshRendererComponent>().material, material);
        EXPECT_EQ(second.get_component<Comet::MeshRendererComponent>().material, material);
        EXPECT_EQ(history.state_id(), cursor);
    }

    TEST_F(EditorAssetsTest, BatchScriptAssignmentRestoresEachBindingsParametersOnUndo) {
        const auto path = Comet::ProjectPaths(root).assets();
        ASSERT_TRUE(
            Comet::write_text_file_atomic(path / "a.lua", "return {properties = {speed = 1}}"));
        ASSERT_TRUE(Comet::write_text_file_atomic(
            path / "b.lua", "return {properties = {enabled = true}}"));
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto a = assets->database().find("a.lua")->handle;
        const auto b = assets->database().find("b.lua")->handle;
        Comet::Scene scene;
        auto first = scene.create_entity();
        auto second = scene.create_entity();
        first.add_component<Comet::ScriptComponent>().asset = a;
        second.add_component<Comet::ScriptComponent>().asset = a;
        auto& one = first.get_component<Comet::ScriptComponent>();
        auto& two = second.get_component<Comet::ScriptComponent>();
        one.parameters = {{"speed", 10.0f}};
        two.parameters = {{"speed", 20.0f}};
        const auto first_before = one.parameters, second_before = two.parameters;
        auto components = Comet::create_scene_component_registry();
        CommandHistory history;
        history.bind_scene(&scene);
        PropertyEditTransaction edit(history, components);
        SelectionService selection(scene);
        EditorState state;
        SceneEditor editor(state, history, edit, components, selection, *assets);
        const SceneEditor::AssetInput input{{b, assets->database().get_revision(b)},
            history.generation(), Comet::AssetType::Script};
        const std::array targets{
            PropertyEditTransaction::Target{first.get_uuid(), "script", "asset"},
            PropertyEditTransaction::Target{second.get_uuid(), "script", "asset"}};
        ASSERT_TRUE(editor.assign_assets(&scene, targets, input));
        EXPECT_EQ(one.asset, b);
        EXPECT_EQ(two.asset, b);
        EXPECT_TRUE(one.parameters.empty());
        EXPECT_TRUE(two.parameters.empty());
        ASSERT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(editor.undo(&scene));
        EXPECT_EQ(one.asset, a);
        EXPECT_EQ(two.asset, a);
        EXPECT_EQ(one.parameters, first_before);
        EXPECT_EQ(two.parameters, second_before);
        ASSERT_TRUE(editor.redo(&scene));
        EXPECT_TRUE(one.parameters.empty());
        EXPECT_TRUE(two.parameters.empty());
    }

    TEST_F(EditorAssetsTest, AssetAssignmentUsesHistoryOnlyForTheEditScene) {
        const auto material = add_material();
        Comet::Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<Comet::MeshRendererComponent>();
        auto components = Comet::create_scene_component_registry();
        CommandHistory history;
        history.bind_scene(&scene);
        PropertyEditTransaction edit(history, components);
        SelectionService selection(scene);
        EditorState state;
        SceneEditor editor(state, history, edit, components, selection, *assets);
        SceneEditor::AssetInput input{{material, assets->database().get_revision(material)},
            history.generation(), Comet::AssetType::Material};
        PropertyEditTransaction::Target target{entity.get_uuid(), "mesh_renderer", "material"};
        ASSERT_TRUE(editor.assign_asset(&scene, target, input));
        EXPECT_EQ(entity.get_component<Comet::MeshRendererComponent>().material, material);
        ASSERT_TRUE(editor.undo(&scene));
        EXPECT_FALSE(entity.get_component<Comet::MeshRendererComponent>().material);
        const auto history_state = history.state_id();

        Comet::Scene runtime_scene;
        auto runtime_entity = runtime_scene.create_entity_with_uuid(entity.get_uuid());
        runtime_entity.add_component<Comet::MeshRendererComponent>();
        state.mode = EditorMode::Play;
        ASSERT_TRUE(editor.assign_asset(&runtime_scene, target, input));
        EXPECT_EQ(runtime_entity.get_component<Comet::MeshRendererComponent>().material, material);
        EXPECT_FALSE(entity.get_component<Comet::MeshRendererComponent>().material);
        EXPECT_EQ(history.state_id(), history_state);
        ++input.generation;
        EXPECT_FALSE(editor.assign_asset(&runtime_scene, target, input));
    }

    TEST_F(EditorAssetsTest, MaterialReadsValidateIdentityAndRevisionWithoutPublishingRuntime) {
        const auto handle = add_material();
        const auto runtime_before = runtime.resolve<Comet::Material>(handle);
        Comet::AssetTarget request{handle, assets->database().get_revision(handle)};
        auto read = assets->read_material(request);
        ASSERT_TRUE(read);
        EXPECT_EQ(read.value().template_name, "test");
        EXPECT_EQ(runtime.resolve<Comet::Material>(handle), runtime_before);
        ++request.revision;
        EXPECT_FALSE(assets->read_material(request));
        EXPECT_FALSE(assets->read_material({mesh, assets->database().get_revision(mesh)}));
    }

    TEST_F(EditorAssetsTest, RestoresSceneEnvironmentReferenceAfterSourceRepair) {
        factory.fail_texture_creation(false);
        const auto path = Comet::ProjectPaths(root).assets() / "studio.hdr";
        Comet::Tests::write_hdr(path);
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto handle = assets->database().find("studio.hdr")->handle;
        Comet::Scene scene;
        const auto components = Comet::create_scene_component_registry();
        ASSERT_TRUE(scene.set_environment({handle, true, 1, 0}));
        assets->track_scene(scene, components);
        ASSERT_TRUE(assets->restore_references());
        scheduler.wait_idle();
        ASSERT_TRUE(assets->update());
        ASSERT_TRUE(runtime.resolve<Comet::Environment>(handle));
        std::filesystem::remove(path);
        ASSERT_TRUE(assets->refresh().snapshot_updated);
        EXPECT_FALSE(runtime.contains(handle));
        ASSERT_TRUE(assets->restore_references());
        EXPECT_EQ(scene.get_environment().asset, handle);
        Comet::Tests::write_hdr(path);
        ASSERT_TRUE(assets->refresh().succeeded());
        ASSERT_EQ(assets->database().find("studio.hdr")->handle, handle);
        ASSERT_TRUE(assets->restore_references());
        scheduler.wait_idle();
        ASSERT_TRUE(assets->update());
        EXPECT_TRUE(runtime.resolve<Comet::Environment>(handle));
    }

    TEST_F(EditorAssetsTest, CreatesMaterialWithStableIdentityThenEditsMovesAndReopens) {
        const auto paths = Comet::ProjectPaths(root);
        std::filesystem::create_directory(paths.assets() / "materials");
        const auto data = make_material_data(*Comet::MaterialLayout::find_builtin("pbr"));
        const auto report = assets->create_material("materials/new.mat", data);
        ASSERT_TRUE(report.snapshot_updated);
        ASSERT_TRUE(report.succeeded());
        const auto* record = assets->database().find("materials/new.mat");
        ASSERT_NE(record, nullptr);
        const auto handle = record->handle;
        EXPECT_EQ(report.added_assets, std::vector{handle});
        EXPECT_EQ(Comet::MaterialSerializer{}.load(paths.assets() / record->path).value(), data);
        EXPECT_EQ(Comet::MetadataSerializer{}
                      .load(Comet::metadata_path(paths.assets() / record->path))
                      .value()
                      .handle,
            handle);
        ASSERT_TRUE(assets->load_reference(
            handle, Comet::AssetType::Material, assets->database().get_revision(handle)));
        const auto original = runtime.resolve<Comet::Material>(handle);
        ASSERT_TRUE(original);
        const auto unlit = make_material_data(*Comet::MaterialLayout::find_builtin("unlit_color"));
        auto update = assets->prepare_material_edit(
            {{handle, assets->database().get_revision(handle)}, MaterialEdit{data, unlit}});
        ASSERT_TRUE(update);
        ASSERT_TRUE(assets->commit_material_edit(update.value()));
        EXPECT_NE(runtime.resolve<Comet::Material>(handle), original);
        ASSERT_TRUE(assets->move(handle, "renamed.mat").snapshot_updated);
        Comet::AssetDatabase reopened(paths);
        ASSERT_TRUE(reopened.scan().succeeded());
        ASSERT_NE(reopened.find(handle), nullptr);
        EXPECT_EQ(reopened.find(handle)->path, "renamed.mat");
        EXPECT_EQ(Comet::MaterialSerializer{}.load(paths.assets() / "renamed.mat").value(), unlit);
    }

    TEST_F(EditorAssetsTest, CreatesComponentScriptWithStableIdentityAndLoadsIt) {
        const auto paths = Comet::ProjectPaths(root);
        std::filesystem::create_directory(paths.assets() / "scripts");
        const auto report = assets->create_script("scripts/new_script.lua");
        ASSERT_TRUE(report.snapshot_updated);
        ASSERT_TRUE(report.succeeded());
        const auto* record = assets->database().find("scripts/new_script.lua");
        ASSERT_NE(record, nullptr);
        EXPECT_EQ(record->type, Comet::AssetType::Script);
        EXPECT_EQ(report.added_assets, std::vector{record->handle});
        const auto source = Comet::read_text_file(paths.assets() / record->path);
        ASSERT_TRUE(source);
        EXPECT_NE(source.value().find("function script:update(dt)"), std::string::npos);
        EXPECT_NE(source.value().find("return script"), std::string::npos);
        EXPECT_EQ(Comet::MetadataSerializer{}
                      .load(Comet::metadata_path(paths.assets() / record->path))
                      .value()
                      .handle,
            record->handle);
        ASSERT_TRUE(assets->load_reference(record->handle, Comet::AssetType::Script,
            assets->database().get_revision(record->handle)));
        EXPECT_TRUE(runtime.resolve<Comet::Script>(record->handle));
        Comet::Scene scene;
        scene.create_entity().add_component<Comet::ScriptComponent>().asset = record->handle;
        Comet::SceneRuntime player;
        ASSERT_TRUE(player.add_system(std::make_unique<Comet::ScriptSystem>(runtime)));
        ASSERT_TRUE(player.start(scene));
        ASSERT_TRUE(player.advance(1.0 / 60.0));
        ASSERT_TRUE(player.stop());
        Comet::AssetDatabase reopened(paths);
        ASSERT_TRUE(reopened.scan().succeeded());
        ASSERT_NE(reopened.find(record->handle), nullptr);
    }

    TEST_F(EditorAssetsTest, CreatesLuaModuleWithoutAddingAssetIdentityOrRuntimeEntry) {
        const auto paths = Comet::ProjectPaths(root);
        std::filesystem::create_directory(paths.assets() / "scripts");
        const auto indexed = assets->database().size();
        const auto registered = runtime.size();

        const auto report = assets->create_script(
            "scripts/shared.module.lua", AssetSourceOperations::ScriptKind::Module);

        ASSERT_TRUE(report.succeeded());
        EXPECT_TRUE(report.snapshot_updated);
        EXPECT_TRUE(report.added_assets.empty());
        EXPECT_EQ(report.generated_metadata, 0u);
        EXPECT_EQ(assets->database().size(), indexed);
        EXPECT_EQ(assets->database().find("scripts/shared.module.lua"), nullptr);
        EXPECT_EQ(runtime.size(), registered);
        const auto path = paths.assets() / "scripts/shared.module.lua";
        EXPECT_TRUE(std::filesystem::is_regular_file(path));
        EXPECT_FALSE(std::filesystem::exists(Comet::metadata_path(path)));
        const auto source = Comet::read_text_file(path);
        ASSERT_TRUE(source);
        EXPECT_EQ(source.value(), "local module = {}\n\nreturn module\n");

        const auto duplicate = assets->create_script(
            "scripts/shared.module.lua", AssetSourceOperations::ScriptKind::Module);
        EXPECT_FALSE(duplicate.succeeded());
        EXPECT_FALSE(duplicate.snapshot_updated);
        EXPECT_EQ(Comet::read_text_file(path).value(), source.value());
        EXPECT_EQ(assets->database().size(), indexed);
        EXPECT_EQ(runtime.size(), registered);
    }

    TEST_F(EditorAssetsTest, CreatingMissingLuaModuleRestoresBothTrackedScriptConsumers) {
        const auto paths = Comet::ProjectPaths(root);
        std::filesystem::create_directory(paths.assets() / "scripts");
        ASSERT_TRUE(Comet::write_text_file_atomic(paths.assets() / "scripts/first.lua", R"(
            local shared = require('scripts.shared')
            return {update = function()
                shared.visits = (shared.visits or 0) + 1
                comet.translate(shared.visits, 0, 0)
            end}
        )"));
        ASSERT_TRUE(Comet::write_text_file_atomic(paths.assets() / "scripts/second.lua", R"(
            local shared = require('scripts.shared')
            return {update = function()
                shared.visits = (shared.visits or 0) + 10
                comet.translate(0, shared.visits, 0)
            end}
        )"));
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto* first_record = assets->database().find("scripts/first.lua");
        const auto* second_record = assets->database().find("scripts/second.lua");
        ASSERT_NE(first_record, nullptr);
        ASSERT_NE(second_record, nullptr);
        const auto first = first_record->handle;
        const auto second = second_record->handle;
        const auto indexed = assets->database().size();
        const auto registered = runtime.size();
        Comet::Scene scene;
        auto left = scene.create_entity();
        auto right = scene.create_entity();
        left.add_component<Comet::ScriptComponent>().asset = first;
        right.add_component<Comet::ScriptComponent>().asset = second;
        const auto components = Comet::create_scene_component_registry();
        assets->track_scene(scene, components);
        ASSERT_TRUE(
            assets->restore_references({.max_results = 8, .max_time = std::chrono::seconds(1)}));
        EXPECT_FALSE(runtime.contains(first));
        EXPECT_FALSE(runtime.contains(second));
        const auto dependents =
            assets->database().get_import_dependents("scripts/shared.module.lua");
        ASSERT_EQ(dependents.size(), 2u);
        EXPECT_NE(std::ranges::find(dependents, first), dependents.end());
        EXPECT_NE(std::ranges::find(dependents, second), dependents.end());

        const auto report = assets->create_script(
            "scripts/shared.module.lua", AssetSourceOperations::ScriptKind::Module);

        ASSERT_TRUE(report.succeeded());
        ASSERT_TRUE(report.snapshot_updated);
        EXPECT_TRUE(report.added_assets.empty());
        EXPECT_EQ(report.modified_assets.size(), 2u);
        EXPECT_NE(std::ranges::find(report.modified_assets, first), report.modified_assets.end());
        EXPECT_NE(std::ranges::find(report.modified_assets, second), report.modified_assets.end());
        EXPECT_EQ(assets->database().size(), indexed);
        EXPECT_EQ(assets->database().find("scripts/shared.module.lua"), nullptr);
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "scripts/shared.module.lua.meta"));
        ASSERT_TRUE(
            assets->restore_references({.max_results = 8, .max_time = std::chrono::seconds(1)}));
        ASSERT_TRUE(runtime.resolve<Comet::Script>(first));
        ASSERT_TRUE(runtime.resolve<Comet::Script>(second));
        EXPECT_EQ(runtime.size(), registered + 2);
        Comet::SceneRuntime player;
        ASSERT_TRUE(player.add_system(std::make_unique<Comet::ScriptSystem>(runtime)));
        ASSERT_TRUE(player.start(scene));
        ASSERT_TRUE(player.advance(0));
        EXPECT_FLOAT_EQ(left.get_component<Comet::TransformComponent>().translation.x, 1);
        EXPECT_FLOAT_EQ(right.get_component<Comet::TransformComponent>().translation.y, 10);
        ASSERT_TRUE(player.advance(0));
        EXPECT_FLOAT_EQ(left.get_component<Comet::TransformComponent>().translation.x, 3);
        EXPECT_FLOAT_EQ(right.get_component<Comet::TransformComponent>().translation.y, 30);
        ASSERT_TRUE(player.stop());
    }

    TEST_F(EditorAssetsTest, SourceMonitorRestoresBothConsumersAfterExternalModuleCreation) {
        complete_imports();
        const auto paths = Comet::ProjectPaths(root);
        for(const auto* path : {"first.lua", "second.lua"}) {
            ASSERT_TRUE(Comet::write_text_file_atomic(paths.assets() / path, R"(
                local shared = require('shared')
                return {update = function() comet.translate(shared.step, 0, 0) end}
            )"));
        }
        const auto discovered = wait_for_source_report();
        ASSERT_TRUE(discovered);
        ASSERT_TRUE(discovered->succeeded());
        const auto* first_record = assets->database().find("first.lua");
        const auto* second_record = assets->database().find("second.lua");
        ASSERT_NE(first_record, nullptr);
        ASSERT_NE(second_record, nullptr);
        const auto first = first_record->handle;
        const auto second = second_record->handle;
        const auto indexed = assets->database().size();
        Comet::Scene scene;
        auto left = scene.create_entity();
        auto right = scene.create_entity();
        left.add_component<Comet::ScriptComponent>().asset = first;
        right.add_component<Comet::ScriptComponent>().asset = second;
        const auto components = Comet::create_scene_component_registry();
        assets->track_scene(scene, components);
        ASSERT_TRUE(
            assets->restore_references({.max_results = 8, .max_time = std::chrono::seconds(1)}));
        ASSERT_FALSE(runtime.contains(first));
        ASSERT_FALSE(runtime.contains(second));
        const auto dependents = assets->database().get_import_dependents("shared.module.lua");
        ASSERT_EQ(dependents.size(), 2u);
        EXPECT_NE(std::ranges::find(dependents, first), dependents.end());
        EXPECT_NE(std::ranges::find(dependents, second), dependents.end());

        ASSERT_TRUE(Comet::write_text_file_atomic(
            paths.assets() / "shared.module.lua", "return {step = 7}"));
        bool first_notified = false;
        bool second_notified = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while(std::chrono::steady_clock::now() < deadline) {
            const auto updated = assets->update();
            ASSERT_TRUE(updated) << updated.error().message;
            if(updated.value()) {
                const auto& report = *updated.value();
                ASSERT_TRUE(report.succeeded());
                EXPECT_TRUE(report.added_assets.empty());
                first_notified |= std::ranges::find(report.modified_assets, first)
                                  != report.modified_assets.end();
                second_notified |= std::ranges::find(report.modified_assets, second)
                                   != report.modified_assets.end();
            }
            ASSERT_TRUE(assets->restore_references(
                {.max_results = 8, .max_time = std::chrono::seconds(1)}));
            if(runtime.contains(first) && runtime.contains(second))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        EXPECT_TRUE(first_notified);
        EXPECT_TRUE(second_notified);
        ASSERT_TRUE(runtime.resolve<Comet::Script>(first));
        ASSERT_TRUE(runtime.resolve<Comet::Script>(second));
        EXPECT_EQ(assets->database().size(), indexed);
        EXPECT_EQ(assets->database().find("shared.module.lua"), nullptr);
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "shared.module.lua.meta"));
        Comet::SceneRuntime player;
        ASSERT_TRUE(player.add_system(std::make_unique<Comet::ScriptSystem>(runtime)));
        ASSERT_TRUE(player.start(scene));
        ASSERT_TRUE(player.advance(0));
        EXPECT_FLOAT_EQ(left.get_component<Comet::TransformComponent>().translation.x, 7);
        EXPECT_FLOAT_EQ(right.get_component<Comet::TransformComponent>().translation.x, 7);
        ASSERT_TRUE(player.stop());
    }

    struct ModuleMoveCase {
        const char* destination;
        const char* reference;
    };

    class EditorAssetsModuleMoveTest: public EditorAssetsTest,
                                      public ::testing::WithParamInterface<ModuleMoveCase> {};

    TEST_P(EditorAssetsModuleMoveTest, KeepsLastGoodGroupUntilBothConsumersUpdateTheirRequire) {
        const auto paths = Comet::ProjectPaths(root);
        const std::filesystem::path destination = GetParam().destination;
        std::filesystem::create_directories((paths.assets() / destination).parent_path());
        constexpr auto old_source = R"(
            local shared = require('shared')
            return {update = function() comet.translate(shared.step, 0, 0) end}
        )";
        const auto new_source =
            "local shared = require('" + std::string(GetParam().reference)
            + "')\nreturn {update = function() comet.translate(shared.step, 0, 0) end}";
        ASSERT_TRUE(Comet::write_text_file_atomic(
            paths.assets() / "shared.module.lua", "return {step = 1}"));
        for(const auto* name : {"first.lua", "second.lua"})
            ASSERT_TRUE(Comet::write_text_file_atomic(paths.assets() / name, old_source));
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto* first_record = assets->database().find("first.lua");
        const auto* second_record = assets->database().find("second.lua");
        ASSERT_NE(first_record, nullptr);
        ASSERT_NE(second_record, nullptr);
        const auto first = first_record->handle;
        const auto second = second_record->handle;
        for(const auto handle : {first, second}) {
            ASSERT_TRUE(assets->load_reference(
                handle, Comet::AssetType::Script, assets->database().get_revision(handle)));
        }
        const auto original_first = runtime.resolve<Comet::Script>(first);
        const auto original_second = runtime.resolve<Comet::Script>(second);
        ASSERT_TRUE(original_first);
        ASSERT_TRUE(original_second);
        const auto indexed = assets->database().size();
        const auto registered = runtime.size();
        Comet::Scene scene;
        auto left = scene.create_entity();
        auto right = scene.create_entity();
        left.add_component<Comet::ScriptComponent>().asset = first;
        right.add_component<Comet::ScriptComponent>().asset = second;
        Comet::SceneRuntime player;
        auto candidate_system = std::make_unique<Comet::ScriptSystem>(runtime);
        auto* script_system = candidate_system.get();
        ASSERT_TRUE(player.add_system(std::move(candidate_system)));
        ASSERT_TRUE(player.start(scene));
        ASSERT_TRUE(player.advance(0));

        const auto renamed = assets->move_module("shared.module.lua", destination);
        ASSERT_TRUE(renamed.succeeded());
        ASSERT_TRUE(renamed.snapshot_updated);
        EXPECT_TRUE(renamed.added_assets.empty());
        EXPECT_TRUE(renamed.removed_assets.empty());
        for(const auto handle : {first, second})
            EXPECT_NE(
                std::ranges::find(renamed.modified_assets, handle), renamed.modified_assets.end());
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "shared.module.lua"));
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / destination));
        EXPECT_EQ(runtime.resolve<Comet::Script>(first), original_first);
        EXPECT_EQ(runtime.resolve<Comet::Script>(second), original_second);
        ASSERT_TRUE(player.advance(0));
        EXPECT_FLOAT_EQ(left.get_component<Comet::TransformComponent>().translation.x, 2);
        EXPECT_FLOAT_EQ(right.get_component<Comet::TransformComponent>().translation.x, 2);

        ASSERT_TRUE(Comet::write_text_file_atomic(paths.assets() / "first.lua", new_source));
        ASSERT_TRUE(assets->refresh().succeeded());
        EXPECT_EQ(runtime.resolve<Comet::Script>(first), original_first);
        EXPECT_EQ(runtime.resolve<Comet::Script>(second), original_second);
        ASSERT_TRUE(player.advance(0));
        EXPECT_FLOAT_EQ(left.get_component<Comet::TransformComponent>().translation.x, 3);
        EXPECT_FLOAT_EQ(right.get_component<Comet::TransformComponent>().translation.x, 3);

        ASSERT_TRUE(Comet::write_text_file_atomic(paths.assets() / "second.lua", new_source));
        ASSERT_TRUE(
            Comet::write_text_file_atomic(paths.assets() / destination, "return {step = 4}"));
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto current_first = runtime.resolve<Comet::Script>(first);
        const auto current_second = runtime.resolve<Comet::Script>(second);
        ASSERT_TRUE(current_first);
        ASSERT_TRUE(current_second);
        EXPECT_NE(current_first, original_first);
        EXPECT_NE(current_second, original_second);
        EXPECT_EQ(script_system->running_script(left), original_first);
        EXPECT_EQ(script_system->running_script(right), original_second);
        ASSERT_TRUE(player.advance(0));
        EXPECT_EQ(script_system->running_script(left), current_first);
        EXPECT_EQ(script_system->running_script(right), current_second);
        EXPECT_FLOAT_EQ(left.get_component<Comet::TransformComponent>().translation.x, 7);
        EXPECT_FLOAT_EQ(right.get_component<Comet::TransformComponent>().translation.x, 7);
        EXPECT_EQ(assets->database().size(), indexed);
        EXPECT_EQ(runtime.size(), registered);
        EXPECT_TRUE(assets->database().get_import_dependents("shared.module.lua").empty());
        EXPECT_EQ(assets->database().get_import_dependents(destination).size(), 2u);
        for(const auto& name : {std::filesystem::path("shared.module.lua"), destination}) {
            EXPECT_EQ(assets->database().find(name), nullptr);
            EXPECT_FALSE(std::filesystem::exists(Comet::metadata_path(paths.assets() / name)));
        }
        ASSERT_TRUE(player.stop());
    }

    INSTANTIATE_TEST_SUITE_P(SameOrNestedDirectory, EditorAssetsModuleMoveTest,
        ::testing::Values(ModuleMoveCase{"tuning.module.lua", "tuning"},
            ModuleMoveCase{"scripts/tuning.module.lua", "scripts.tuning"}));

    TEST_F(EditorAssetsTest, ModuleDeletionKeepsRunningScriptsAndRecoversColdConsumersAfterRepair) {
        const auto paths = Comet::ProjectPaths(root);
        const auto module = paths.assets() / "shared.module.lua";
        ASSERT_TRUE(Comet::write_text_file_atomic(module, "return {step = 1}"));
        for(const auto* name : {"first.lua", "second.lua"}) {
            ASSERT_TRUE(Comet::write_text_file_atomic(paths.assets() / name, R"(
                local shared = require('shared')
                return {update = function() comet.translate(shared.step, 0, 0) end}
            )"));
        }
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto* first_record = assets->database().find("first.lua");
        const auto* second_record = assets->database().find("second.lua");
        ASSERT_NE(first_record, nullptr);
        ASSERT_NE(second_record, nullptr);
        const auto first = first_record->handle;
        const auto second = second_record->handle;
        const auto revision = assets->database().get_revision(first);
        const auto indexed = assets->database().size();
        ASSERT_TRUE(assets->load_reference(first, Comet::AssetType::Script, revision));
        const auto original = runtime.resolve<Comet::Script>(first);
        ASSERT_TRUE(original);
        ASSERT_FALSE(runtime.contains(second));
        Comet::Scene scene;
        auto left = scene.create_entity();
        left.add_component<Comet::ScriptComponent>().asset = first;
        Comet::SceneRuntime player;
        auto candidate_system = std::make_unique<Comet::ScriptSystem>(runtime);
        auto* script_system = candidate_system.get();
        ASSERT_TRUE(player.add_system(std::move(candidate_system)));
        ASSERT_TRUE(player.start(scene));
        ASSERT_TRUE(player.advance(0));

        reject_trash = true;
        const auto failed = assets->remove_module("shared.module.lua");
        EXPECT_FALSE(failed.succeeded());
        EXPECT_FALSE(failed.snapshot_updated);
        EXPECT_TRUE(std::filesystem::is_regular_file(module));
        EXPECT_TRUE(assets->database().is_current(first, revision));
        EXPECT_EQ(runtime.resolve<Comet::Script>(first), original);

        reject_trash = false;
        const auto removed = assets->remove_module("shared.module.lua");
        ASSERT_TRUE(removed.succeeded());
        ASSERT_TRUE(removed.snapshot_updated);
        EXPECT_TRUE(removed.added_assets.empty());
        EXPECT_TRUE(removed.removed_assets.empty());
        EXPECT_EQ(removed.generated_metadata, 0u);
        EXPECT_NE(std::ranges::find(removed.modified_assets, first), removed.modified_assets.end());
        EXPECT_EQ(trash_sources, (std::vector<std::filesystem::path>{module, module}));
        EXPECT_FALSE(std::filesystem::exists(module));
        EXPECT_TRUE(std::filesystem::is_regular_file(root / "fake-system-trash/shared.module.lua"));
        EXPECT_EQ(runtime.resolve<Comet::Script>(first), original);
        EXPECT_EQ(left.get_component<Comet::ScriptComponent>().asset, first);
        ASSERT_TRUE(player.advance(0));
        EXPECT_EQ(script_system->running_script(left), original);
        EXPECT_FLOAT_EQ(left.get_component<Comet::TransformComponent>().translation.x, 2);

        const auto cold = assets->load_reference(
            second, Comet::AssetType::Script, assets->database().get_revision(second));
        ASSERT_FALSE(cold);
        EXPECT_NE(cold.error().message.find("shared.module.lua"), std::string::npos);
        EXPECT_FALSE(runtime.contains(second));
        EXPECT_EQ(assets->database().get_import_dependents("shared.module.lua").size(), 2u);

        ASSERT_TRUE(Comet::write_text_file_atomic(module, "return {step = 4}"));
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto repaired = runtime.resolve<Comet::Script>(first);
        ASSERT_TRUE(repaired);
        EXPECT_NE(repaired, original);
        ASSERT_TRUE(assets->load_reference(
            second, Comet::AssetType::Script, assets->database().get_revision(second)));
        ASSERT_TRUE(runtime.resolve<Comet::Script>(second));
        EXPECT_EQ(script_system->running_script(left), original);
        auto right = scene.create_entity();
        right.add_component<Comet::ScriptComponent>().asset = second;
        ASSERT_TRUE(player.advance(0));
        EXPECT_EQ(script_system->running_script(left), repaired);
        EXPECT_FLOAT_EQ(left.get_component<Comet::TransformComponent>().translation.x, 6);
        EXPECT_FLOAT_EQ(right.get_component<Comet::TransformComponent>().translation.x, 4);
        EXPECT_EQ(assets->database().size(), indexed);
        EXPECT_EQ(assets->database().find("shared.module.lua"), nullptr);
        EXPECT_FALSE(std::filesystem::exists(Comet::metadata_path(module)));
        EXPECT_FALSE(std::filesystem::exists(paths.local_data() / "trash"));
        ASSERT_TRUE(player.stop());
    }

    TEST_F(EditorAssetsTest, ScriptSourceRefreshKeepsLastGoodVersionAndReloadsAfterRepair) {
        const auto path = Comet::ProjectPaths(root).assets() / "reload.lua";
        ASSERT_TRUE(Comet::write_text_file_atomic(
            path, "return {update = function() comet.translate(1, 0, 0) end}"));
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto* record = assets->database().find("reload.lua");
        ASSERT_NE(record, nullptr);
        const auto handle = record->handle;
        ASSERT_TRUE(assets->load_reference(
            handle, Comet::AssetType::Script, assets->database().get_revision(handle)));
        const auto original = runtime.resolve<Comet::Script>(handle);
        ASSERT_TRUE(original);
        Comet::Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<Comet::ScriptComponent>().asset = handle;
        Comet::SceneRuntime player;
        auto candidate_system = std::make_unique<Comet::ScriptSystem>(runtime);
        auto* script_system = candidate_system.get();
        ASSERT_TRUE(player.add_system(std::move(candidate_system)));
        ASSERT_TRUE(player.start(scene));
        ASSERT_TRUE(player.advance(0));

        for(const auto* invalid :
            {"return {", "return {properties = {unsupported = function() end}}"}) {
            ASSERT_TRUE(Comet::write_text_file_atomic(path, invalid));
            const auto report = assets->refresh();
            ASSERT_TRUE(report.succeeded());
            ASSERT_NE(
                std::ranges::find(report.modified_assets, handle), report.modified_assets.end());
            EXPECT_EQ(runtime.resolve<Comet::Script>(handle), original);
            ASSERT_TRUE(player.advance(0));
            EXPECT_EQ(script_system->running_script(entity), original);
        }
        EXPECT_FLOAT_EQ(entity.get_component<Comet::TransformComponent>().translation.x, 3);

        ASSERT_TRUE(Comet::write_text_file_atomic(
            path, "return {update = function() comet.translate(-10, 0, 0) end}"));
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto repaired = runtime.resolve<Comet::Script>(handle);
        ASSERT_TRUE(repaired);
        ASSERT_NE(repaired, original);
        EXPECT_EQ(script_system->running_script(entity), original);
        ASSERT_TRUE(player.advance(0));
        EXPECT_EQ(script_system->running_script(entity), repaired);
        EXPECT_FLOAT_EQ(entity.get_component<Comet::TransformComponent>().translation.x, -7);
        ASSERT_TRUE(player.stop());
    }

    TEST_F(EditorAssetsTest, ModuleReloadLogsTheCompleteTraceAndKeepsLastGoodVersion) {
        const auto paths = Comet::ProjectPaths(root);
        const auto module_path = paths.assets() / "tuning.module.lua";
        ASSERT_TRUE(Comet::write_text_file_atomic(module_path, "return {step = 1}"));
        ASSERT_TRUE(Comet::write_text_file_atomic(paths.assets() / "actor.lua", R"(
            local tuning = require('tuning')
            return {update = function() comet.translate(tuning.step, 0, 0) end}
        )"));
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto* record = assets->database().find("actor.lua");
        ASSERT_NE(record, nullptr);
        const auto handle = record->handle;
        ASSERT_TRUE(assets->load_reference(
            handle, Comet::AssetType::Script, assets->database().get_revision(handle)));
        const auto original = runtime.resolve<Comet::Script>(handle);
        ASSERT_TRUE(original);
        Comet::Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<Comet::ScriptComponent>().asset = handle;
        Comet::SceneRuntime player;
        auto candidate_system = std::make_unique<Comet::ScriptSystem>(runtime);
        auto* script_system = candidate_system.get();
        ASSERT_TRUE(player.add_system(std::move(candidate_system)));
        ASSERT_TRUE(player.start(scene));
        ASSERT_TRUE(player.advance(0));

        ASSERT_TRUE(Comet::write_text_file_atomic(module_path, R"(
            local function initialize_tuning()
                error('tuning initialization failed')
            end
            initialize_tuning()
            return {step = 2}
        )"));
        const std::array<std::filesystem::path, 1> roots{"actor.lua"};
        const auto rejected = Comet::Script::load_group(paths.assets(), roots);
        ASSERT_FALSE(rejected);
        EXPECT_NE(rejected.error().message.find("tuning initialization failed"), std::string::npos);
        EXPECT_NE(rejected.error().message.find("tuning.module.lua:"), std::string::npos);
        EXPECT_NE(rejected.error().message.find("actor.lua:"), std::string::npos);
        EXPECT_NE(rejected.error().message.find("stack traceback:"), std::string::npos);

        Comet::LogSettings config;
        config.enable_file_logging = false;
        Comet::Logger::init(config);
        const auto logger = Comet::Logger::get_console_logger();
        std::vector<std::string> warnings;
        const auto sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
            [&](const spdlog::details::log_msg& message) {
                const std::string_view text(message.payload.data(), message.payload.size());
                if(text.starts_with("Cannot refresh script group containing "))
                    warnings.emplace_back(text);
            });
        logger->sinks().push_back(sink);
        const Comet::ScopeExit remove_sink([&] { std::erase(logger->sinks(), sink); });
        ASSERT_TRUE(assets->refresh().succeeded());
        ASSERT_EQ(warnings.size(), 1u);
        EXPECT_TRUE(warnings.front().ends_with(rejected.error().message)) << warnings.front();
        EXPECT_EQ(runtime.resolve<Comet::Script>(handle), original);
        ASSERT_TRUE(player.advance(0));
        EXPECT_EQ(script_system->running_script(entity), original);
        EXPECT_FLOAT_EQ(entity.get_component<Comet::TransformComponent>().translation.x, 2);

        ASSERT_TRUE(Comet::write_text_file_atomic(module_path, "return {step = 2}"));
        ASSERT_TRUE(assets->refresh().succeeded());
        EXPECT_NE(runtime.resolve<Comet::Script>(handle), original);
        ASSERT_TRUE(player.advance(0));
        EXPECT_FLOAT_EQ(entity.get_component<Comet::TransformComponent>().translation.x, 4);
        ASSERT_TRUE(player.stop());
    }

    TEST_F(EditorAssetsTest, ScriptCreationRejectsConflictsAndRollsBackOnIndexFailure) {
        const auto paths = Comet::ProjectPaths(root);
        ASSERT_TRUE(assets->create_script("new.lua").succeeded());
        const auto handle = assets->database().find("new.lua")->handle;
        const auto revision = assets->database().get_revision(handle);
        for(const auto& path :
            {"new.lua", "../escape.lua", "missing/new.lua", "bad.txt", ".comet-tmp-hidden.lua"}) {
            const auto report = assets->create_script(path);
            EXPECT_FALSE(report.snapshot_updated) << path;
            EXPECT_FALSE(report.succeeded()) << path;
        }
        EXPECT_FALSE(assets->create_script(paths.assets() / "absolute.lua").snapshot_updated);
        EXPECT_TRUE(assets->database().is_current(handle, revision));
        std::ofstream(paths.assets() / "broken.mat.meta") << "invalid metadata";
        std::ofstream(paths.assets() / "broken.mat") << "{}";
        const auto failed = assets->create_script("rolled_back.lua");
        EXPECT_FALSE(failed.snapshot_updated);
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "rolled_back.lua"));
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "rolled_back.lua.meta"));
    }

    TEST_F(EditorAssetsTest, DeletingAssetMovesSourceAndIdentityToSystemTrash) {
        const auto paths = Comet::ProjectPaths(root);
        ASSERT_TRUE(assets->create_script("remove_me.lua").succeeded());
        const auto handle = assets->database().find("remove_me.lua")->handle;
        ASSERT_TRUE(assets->load_reference(
            handle, Comet::AssetType::Script, assets->database().get_revision(handle)));
        ASSERT_TRUE(runtime.resolve<Comet::Script>(handle));
        const auto report = assets->remove(handle);
        ASSERT_TRUE(report.snapshot_updated);
        ASSERT_TRUE(report.succeeded());
        EXPECT_EQ(report.removed_assets, std::vector{handle});
        EXPECT_FALSE(assets->database().find(handle));
        EXPECT_FALSE(runtime.resolve<Comet::Script>(handle));
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "remove_me.lua"));
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "remove_me.lua.meta"));
        ASSERT_EQ(trash_requests, 2);
        EXPECT_EQ(trash_sources[0], paths.assets() / "remove_me.lua");
        EXPECT_EQ(trash_sources[1], paths.assets() / "remove_me.lua.meta");
        const auto trash = root / "fake-system-trash";
        ASSERT_TRUE(std::filesystem::exists(trash / "remove_me.lua"));
        ASSERT_TRUE(std::filesystem::exists(trash / "remove_me.lua.meta"));
        const auto metadata = Comet::MetadataSerializer{}.load(trash / "remove_me.lua.meta");
        ASSERT_TRUE(metadata);
        EXPECT_EQ(metadata.value().handle, handle);
        EXPECT_FALSE(std::filesystem::exists(paths.local_data() / "trash"));
    }

    TEST_F(EditorAssetsTest, DeletingAssetRejectsIndexedDependentsAndRollsBackFailedScan) {
        const auto paths = Comet::ProjectPaths(root);
        std::ofstream(paths.assets() / "albedo.png") << "not decoded by scan";
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto texture = assets->database().find("albedo.png")->handle;
        Comet::MaterialData material_data{
            .template_name = "pbr", .texture_properties = {{"base_color_texture", texture}}};
        ASSERT_TRUE(assets->create_material("uses_albedo.mat", material_data).succeeded());
        EXPECT_FALSE(assets->remove(texture).snapshot_updated);
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "albedo.png"));
        ASSERT_TRUE(assets->create_script("keep.lua").succeeded());
        const auto script = assets->database().find("keep.lua")->handle;
        std::ofstream(paths.assets() / "broken.mat") << "{}";
        std::ofstream(paths.assets() / "broken.mat.meta") << "invalid metadata";
        const auto report = assets->remove(script);
        EXPECT_FALSE(report.snapshot_updated);
        EXPECT_EQ(trash_requests, 0);
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "keep.lua"));
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "keep.lua.meta"));
        EXPECT_NE(assets->database().find(script), nullptr);
    }

    TEST_F(EditorAssetsTest, DeletingAssetRollsBackWhenSystemTrashFails) {
        const auto paths = Comet::ProjectPaths(root);
        ASSERT_TRUE(assets->create_script("keep.lua").succeeded());
        const auto handle = assets->database().find("keep.lua")->handle;
        reject_trash = true;

        const auto report = assets->remove(handle);
        EXPECT_FALSE(report.snapshot_updated);
        EXPECT_FALSE(report.succeeded());
        EXPECT_EQ(trash_requests, 1);
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "keep.lua"));
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "keep.lua.meta"));
        EXPECT_NE(assets->database().find(handle), nullptr);
        EXPECT_TRUE(std::filesystem::is_empty(paths.local_data() / "pending-deletions"));
    }

    TEST_F(EditorAssetsTest, DeletingAssetRestoresThePairWhenMetadataTrashFails) {
        const auto paths = Comet::ProjectPaths(root);
        ASSERT_TRUE(assets->create_script("keep.lua").succeeded());
        const auto handle = assets->database().find("keep.lua")->handle;
        reject_metadata_trash = true;

        const auto report = assets->remove(handle);
        EXPECT_FALSE(report.snapshot_updated);
        EXPECT_FALSE(report.succeeded());
        EXPECT_EQ(trash_requests, 2);
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "keep.lua"));
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "keep.lua.meta"));
        EXPECT_NE(assets->database().find(handle), nullptr);
        EXPECT_TRUE(std::filesystem::exists(root / "fake-system-trash/keep.lua"));
        EXPECT_FALSE(std::filesystem::equivalent(
            paths.assets() / "keep.lua", root / "fake-system-trash/keep.lua"));
        EXPECT_TRUE(std::filesystem::is_empty(paths.local_data() / "pending-deletions"));
    }

    TEST_F(EditorAssetsTest, MaterialCreationRejectsCollisionsUnsafePathsAndInvalidData) {
        const auto paths = Comet::ProjectPaths(root);
        const auto data = make_material_data(*Comet::MaterialLayout::find_builtin("pbr"));
        ASSERT_TRUE(assets->create_material("existing.mat", data).snapshot_updated);
        const auto handle = assets->database().find("existing.mat")->handle;
        const auto revision = assets->database().get_revision(handle);
        const auto changed =
            make_material_data(*Comet::MaterialLayout::find_builtin("unlit_color"));
        for(const auto& path : {"existing.mat", "../escape.mat", "missing/new.mat", "bad.txt",
                ".comet-tmp-hidden.mat"}) {
            const auto report = assets->create_material(path, changed);
            EXPECT_FALSE(report.snapshot_updated) << path;
            EXPECT_FALSE(report.succeeded()) << path;
        }
        EXPECT_FALSE(
            assets->create_material(paths.assets() / "absolute.mat", data).snapshot_updated);
        EXPECT_FALSE(assets->create_material("invalid.mat", {}).snapshot_updated);
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "invalid.mat"));
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "invalid.mat.meta"));
        EXPECT_TRUE(assets->database().is_current(handle, revision));
        EXPECT_EQ(Comet::MaterialSerializer{}.load(paths.assets() / "existing.mat").value(), data);
        std::ofstream(paths.assets() / "occupied.mat.meta") << "reserved";
        EXPECT_FALSE(assets->create_material("occupied.mat", data).snapshot_updated);
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "occupied.mat"));
    }

    TEST_F(EditorAssetsTest, MaterialCreationRejectsEscapingSymlinksAndUnavailableStaging) {
        const auto paths = Comet::ProjectPaths(root);
        const auto outside = root / "outside";
        std::filesystem::create_directory(outside);
        std::error_code error;
        std::filesystem::create_directory_symlink(outside, paths.assets() / "link", error);
        if(error)
            GTEST_SKIP() << "Directory symlinks unavailable: " << error.message();
        const auto data = make_material_data(*Comet::MaterialLayout::find_builtin("pbr"));
        EXPECT_FALSE(assets->create_material("link/escape.mat", data).snapshot_updated);
        EXPECT_FALSE(std::filesystem::exists(outside / "escape.mat"));
        std::filesystem::create_directories(paths.cache());
        std::ofstream(paths.cache() / "material-create") << "not a directory";
        EXPECT_FALSE(assets->create_material("new.mat", data).snapshot_updated);
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "new.mat"));
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "new.mat.meta"));
    }

    TEST_F(EditorAssetsTest, MaterialCreationRollsBackWhenIndexCannotCommit) {
        const auto paths = Comet::ProjectPaths(root);
        std::ofstream(paths.assets() / "broken.mat") << "{}";
        std::ofstream(paths.assets() / "broken.mat.meta") << "invalid metadata";
        const auto size = assets->database().size();
        const auto report = assets->create_material(
            "new.mat", make_material_data(*Comet::MaterialLayout::find_builtin("pbr")));
        EXPECT_FALSE(report.snapshot_updated);
        EXPECT_FALSE(report.succeeded());
        EXPECT_EQ(assets->database().size(), size);
        EXPECT_FALSE(assets->database().find("new.mat"));
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "new.mat"));
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "new.mat.meta"));
    }

    TEST_F(EditorAssetsTest,
        PreparedMaterialEditDoesNotPublishUntilCommittedAndRejectsStaleCandidate) {
        const auto material = add_material();
        const auto original = runtime.resolve<Comet::Material>(material);
        const auto path = Comet::ProjectPaths(root).assets() / "placement.mat";
        const auto original_data = Comet::MaterialSerializer{}.load(path).value();
        const auto revision = assets->database().get_revision(material);
        const auto data = make_material_data(*Comet::MaterialLayout::find_builtin("pbr"));
        const AssetEdit edit{{material, revision}, MaterialEdit{original_data, data}};
        auto first = assets->prepare_material_edit(edit);
        auto stale = assets->prepare_material_edit(edit);
        ASSERT_TRUE(first);
        ASSERT_TRUE(stale);
        EXPECT_EQ(runtime.resolve<Comet::Material>(material), original);
        EXPECT_EQ(Comet::MaterialSerializer{}.load(path).value(), original_data);
        EXPECT_EQ(assets->database().get_revision(material), revision);
        ASSERT_TRUE(assets->commit_material_edit(first.value()));
        EXPECT_EQ(runtime.resolve<Comet::Material>(material), first.value().material());
        EXPECT_EQ(Comet::MaterialSerializer{}.load(path).value(), data);
        EXPECT_FALSE(assets->commit_material_edit(stale.value()));
        EXPECT_EQ(runtime.resolve<Comet::Material>(material), first.value().material());
    }

    TEST_F(EditorAssetsTest, FailedPreparedMaterialSaveKeepsResidentVersionAndCanRetry) {
        const auto material = add_material();
        const auto original = runtime.resolve<Comet::Material>(material);
        const auto path = Comet::ProjectPaths(root).assets() / "placement.mat";
        const auto original_data = Comet::MaterialSerializer{}.load(path).value();
        const auto data = make_material_data(*Comet::MaterialLayout::find_builtin("pbr"));
        auto update =
            assets->prepare_material_edit({{material, assets->database().get_revision(material)},
                MaterialEdit{original_data, data}});
        ASSERT_TRUE(update);
        const auto backup = path.parent_path() / "backup";
        std::filesystem::rename(path, backup);
        std::filesystem::create_directory(path);
        EXPECT_FALSE(assets->commit_material_edit(update.value()));
        EXPECT_EQ(runtime.resolve<Comet::Material>(material), original);
        std::filesystem::remove(path);
        std::filesystem::rename(backup, path);
        EXPECT_EQ(Comet::MaterialSerializer{}.load(path).value(), original_data);
        ASSERT_TRUE(assets->commit_material_edit(update.value()));
        EXPECT_EQ(runtime.resolve<Comet::Material>(material), update.value().material());
        EXPECT_EQ(Comet::MaterialSerializer{}.load(path).value(), data);
    }

    TEST_F(EditorAssetsTest, StaleMaterialEditPreservesFileAndResidentVersion) {
        const auto material = add_material();
        const AssetEdit edit{{material, assets->database().get_revision(material)},
            MaterialEdit{{}, {.template_name = "updated"}}};
        auto update = assets->prepare_material_edit(edit);
        ASSERT_TRUE(update);
        ASSERT_TRUE(assets->commit_material_edit(update.value()));
        const auto published = runtime.resolve<Comet::Material>(material);
        ASSERT_TRUE(published);
        const auto revision = assets->database().get_revision(material);
        ASSERT_NE(revision, edit.target.revision);
        const auto path = Comet::ProjectPaths(root).assets() / "placement.mat";
        const auto modified = std::filesystem::last_write_time(path);

        auto stale = edit;
        std::get<MaterialEdit>(stale.value).after.template_name = "stale";
        const auto rejected = assets->prepare_material_edit(stale);
        ASSERT_FALSE(rejected);
        EXPECT_EQ(rejected.error().message, "Asset edit revision is stale");
        EXPECT_EQ(runtime.resolve<Comet::Material>(material), published);
        EXPECT_EQ(assets->database().get_revision(material), revision);
        EXPECT_EQ(std::filesystem::last_write_time(path), modified);
        const auto saved = Comet::MaterialSerializer{}.load(path);
        ASSERT_TRUE(saved);
        EXPECT_EQ(saved.value().template_name, "updated");
    }

    TEST_F(EditorAssetsTest, ReferenceRecoveryUsesCachedReferencesAndHonorsBudget) {
        const auto source = Comet::ProjectPaths(root).assets() / "model.gltf";
        std::filesystem::copy_file(source, source.parent_path() / "second.gltf");
        ASSERT_TRUE(Comet::MetadataSerializer{}.save(
            Comet::AssetMetadata{
                .handle = Comet::AssetHandle::generate(), .type = Comet::AssetType::Mesh},
            Comet::metadata_path(source.parent_path() / "second.gltf")));
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto second = assets->database().find("second.gltf")->handle;
        complete_imports();
        const auto components = Comet::create_scene_component_registry();
        Comet::Scene scene;
        scene.create_entity().add_component<Comet::MeshRendererComponent>(
            mesh, Comet::AssetHandle{});
        auto extra = scene.create_entity();
        extra.add_component<Comet::MeshRendererComponent>(second, Comet::AssetHandle{});
        assets->track_scene(scene, components);
        EXPECT_EQ(assets->restore_references({1, std::chrono::seconds(1)}).value(), 1);
        EXPECT_EQ(factory.mesh_creation_count(), 1);
        EXPECT_EQ(assets->restore_references({1, std::chrono::seconds(1)}).value(), 1);
        EXPECT_EQ(factory.mesh_creation_count(), 2);
        EXPECT_EQ(assets->restore_references().value(), 0);
        // 无关源变化不应重新检查当前场景的驻留引用。
        static_cast<void>(add_material());
        EXPECT_EQ(assets->restore_references().value(), 0);
        assets->track_scene(scene, components);
        EXPECT_EQ(assets->restore_references().value(), 0);
        EXPECT_EQ(factory.mesh_creation_count(), 2);
    }

    TEST_F(EditorAssetsTest, ReferenceRecoveryDropsRemovedSceneReferencesAndPreservesDeviceErrors) {
        complete_imports();
        const auto components = Comet::create_scene_component_registry();
        Comet::Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<Comet::MeshRendererComponent>(mesh, Comet::AssetHandle{});
        assets->track_scene(scene, components);
        Comet::Scene empty;
        assets->track_scene(empty, components);
        EXPECT_EQ(assets->restore_references().value(), 0);
        EXPECT_EQ(factory.mesh_creation_count(), 0);
        assets->track_scene(scene, components);
        factory.fail_mesh_creation(true);
        factory.set_failure_result(vk::Result::eErrorDeviceLost);
        const auto restored = assets->restore_references();
        ASSERT_FALSE(restored);
        EXPECT_TRUE(Comet::is_device_lost(restored.error()));
    }

    TEST_F(EditorAssetsTest, AutomaticModelAndMeshImportsSurviveDefaultQueueCapacity) {
        const auto source = Comet::ProjectPaths(root).assets() / "model.gltf";
        for(int i = 0; i < 140; ++i)
            std::filesystem::copy_file(
                source, source.parent_path() / (std::to_string(i) + ".gltf"));
        ASSERT_TRUE(assets->refresh().succeeded());
        for(int i = 0; i < 160; ++i) {
            ASSERT_TRUE(assets->update());
            scheduler.wait_idle();
        }
        for(const auto& record : assets->database().get_assets()) {
            if(record.source_asset)
                continue;
            if(record.type == Comet::AssetType::Model) {
                EXPECT_TRUE(assets->load_model(
                    {record.handle, assets->database().get_revision(record.handle)}));
            } else if(record.type == Comet::AssetType::Mesh) {
                EXPECT_TRUE(Comet::MeshArtifact::load(artifact_path(), record.handle));
            }
        }
        EXPECT_EQ(factory.mesh_creation_count(), 0);
    }

    TEST_F(EditorAssetsTest, RemovedAssetEditIsRejectedBeforePublication) {
        const auto material = add_material();
        const AssetEdit edit{{material, assets->database().get_revision(material)}, MaterialEdit{}};
        const auto path = Comet::ProjectPaths(root).assets() / "placement.mat";
        ASSERT_TRUE(std::filesystem::remove(path));
        ASSERT_TRUE(std::filesystem::remove(Comet::metadata_path(path)));
        ASSERT_TRUE(assets->refresh().succeeded());
        EXPECT_FALSE(assets->prepare_material_edit(edit));
        EXPECT_FALSE(std::filesystem::exists(path));
        EXPECT_FALSE(runtime.contains(material));
    }

    TEST_F(EditorAssetsTest, UpdateReturnsDeviceLossAndPreservesResidentMesh) {
        complete_imports();
        ASSERT_TRUE(assets->load_reference(
            mesh, Comet::AssetType::Mesh, assets->database().get_revision(mesh)));
        const auto original = runtime.resolve<Comet::Mesh>(mesh);
        ASSERT_TRUE(original);
        factory.fail_mesh_creation(true);
        factory.set_failure_result(vk::Result::eErrorDeviceLost);
        assets->request_source_reimport(mesh);
        scheduler.wait_idle();

        const auto result = assets->update();
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code,
            (Comet::GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
        EXPECT_EQ(runtime.resolve<Comet::Mesh>(mesh), original);
    }

    TEST_F(EditorAssetsTest, MaterialEditingPreservesDependencyDeviceErrorAndOldMaterial) {
        const auto directory = Comet::ProjectPaths(root).assets();
        std::filesystem::copy_file(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                       / "assets/textures/awesomeface.png",
            directory / "texture.png");
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto texture = assets->database().find("texture.png")->handle;
        const auto material = add_material();
        const auto original = runtime.resolve<Comet::Material>(material);
        ASSERT_TRUE(original);
        factory.set_failure_result(vk::Result::eErrorDeviceLost);
        const auto reimported = assets->apply_texture_edit(
            {{texture, assets->database().get_revision(texture)}, TextureEdit{}});
        ASSERT_FALSE(reimported);
        EXPECT_TRUE(Comet::is_device_lost(reimported.error()));
        const auto updated =
            assets->prepare_material_edit({{material, assets->database().get_revision(material)},
                MaterialEdit{{},
                    {.template_name = "changed", .texture_properties = {{"albedo", texture}}}}});
        ASSERT_FALSE(updated);
        EXPECT_EQ(updated.error().code, reimported.error().code);
        EXPECT_NE(updated.error().message.find("albedo"), std::string::npos);
        EXPECT_EQ(runtime.resolve<Comet::Material>(material), original);
        const auto saved = Comet::MaterialSerializer{}.load(directory / "placement.mat");
        ASSERT_TRUE(saved);
        EXPECT_EQ(saved.value().template_name, "test");
        EXPECT_TRUE(saved.value().texture_properties.empty());
    }

    TEST_F(EditorAssetsTest, ExternalProjectOpensStartupSceneAndRecoversAfterInitialImport) {
        const auto directory = Comet::ProjectPaths(root).assets();
        const auto source = std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets";
        for(const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
            if(!entry.is_regular_file() || entry.path().extension() == ".hdr"
                || entry.path().filename() == ".DS_Store")
                continue;
            const auto target = directory / entry.path().lexically_relative(source);
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(entry.path(), target);
            // 仓库只保存环境资源的元数据；测试用微型 HDR 替代可选的大文件下载。
            if(target.extension() == ".meta" && target.stem().extension() == ".hdr")
                Comet::Tests::write_hdr(target.parent_path() / target.stem());
        }
        std::filesystem::copy_file(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "project.json",
            root / "project.json");
        auto project_result = Comet::Project::load(root);
        ASSERT_TRUE(project_result) << project_result.error();
        auto project = std::move(project_result).value();
        assets = std::make_unique<EditorAssets>(project.paths(), runtime, factory, scheduler);
        ASSERT_TRUE(assets->refresh().succeeded());
        factory.fail_texture_creation(false);
        const auto components = Comet::create_scene_component_registry();
        const Comet::SceneSerializer serializer(components);
        std::unique_ptr<Comet::Scene> active;
        std::size_t missing = 0;
        CommandHistory history;
        SceneDocument document(
            serializer, project.paths(), history, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement) {
                missing = assets->prepare_scene(*replacement, components).value();
                active.swap(replacement);
                return Comet::Result<void, Comet::Error>::success();
            });

        const auto resolved = project.paths().resolve_asset_path(project.startup_scene());
        ASSERT_TRUE(resolved) << resolved.error();
        const auto path = resolved.value().string();
        const auto startup = serializer.load(path);
        ASSERT_TRUE(startup) << startup.error();
        const auto initial_entities = startup.value()->entity_count();
        const auto expected_references = components.collect_asset_references(
            *startup.value(), Comet::ComponentRegistry::ReferenceScope::Runtime);
        const auto expected_meshes = std::ranges::count_if(expected_references,
            [](const auto& reference) { return reference.type == Comet::AssetType::Mesh; });
        const auto default_meshes = std::ranges::count_if(expected_references, [](const auto& ref) {
            return ref.type == Comet::AssetType::Mesh && Comet::find_default_asset(ref.handle);
        });
        ASSERT_GT(expected_meshes, 0);
        ASSERT_TRUE(document.open(project.startup_scene().string()));
        ASSERT_NE(active, nullptr);
        EXPECT_EQ(document.get_path(), path);
        // 项目 Mesh、模型子材质与 Shader 材质等待导入；内置资源可直接加载。
        const auto deferred_assets =
            std::ranges::count_if(expected_references, [&](const auto& ref) {
                if(Comet::find_default_asset(ref.handle))
                    return false;
                if(ref.type == Comet::AssetType::Mesh)
                    return true;
                const auto* record = assets->database().find(ref.handle);
                if(record && record->source_asset)
                    return true;
                return ref.type == Comet::AssetType::Material && record
                       && std::ranges::any_of(record->dependencies, [&](const auto dependency) {
                              const auto* input = assets->database().find(dependency);
                              return input && input->type == Comet::AssetType::ShaderProgram;
                          });
            });
        EXPECT_EQ(missing, deferred_assets);
        EXPECT_EQ(factory.mesh_creation_count(), default_meshes);
        EXPECT_EQ(active->entity_count(), initial_entities);
        const auto references = components.collect_asset_references(
            *active, Comet::ComponentRegistry::ReferenceScope::Runtime);
        EXPECT_EQ(references, expected_references);
        for(const auto& reference : references) {
            if(Comet::find_default_asset(reference.handle))
                continue;
            const auto* record = assets->database().find(reference.handle);
            ASSERT_NE(record, nullptr);
            EXPECT_EQ(record->type, reference.type);
        }
        auto original_result = serializer.serialize(*active);
        ASSERT_TRUE(original_result) << original_result.error();
        auto original = std::move(original_result).value();
        complete_imports();
        assets->track_scene(*active, components);
        ASSERT_TRUE(assets->restore_references({100, std::chrono::seconds(1)}));
        EXPECT_EQ(factory.mesh_creation_count(), expected_meshes);
        const auto serialized_again = serializer.serialize(*active);
        ASSERT_TRUE(serialized_again) << serialized_again.error();
        EXPECT_EQ(serialized_again.value(), original);
        for(const auto& reference : references)
            EXPECT_TRUE(runtime.contains(reference.handle));

        active->create_entity("Saved startup edit");
        ASSERT_TRUE(document.save(document.get_path()));
        active.reset();
        ASSERT_TRUE(document.open(path));
        EXPECT_EQ(active->entity_count(), initial_entities + 1);
        EXPECT_EQ(missing, 0U);
        EXPECT_EQ(factory.mesh_creation_count(), expected_meshes);
    }

    TEST_F(EditorAssetsTest, ReopenedScenePreparesSharedReferencesFromArtifacts) {
        const auto material = add_material();
        complete_imports();
        const auto components = Comet::create_scene_component_registry();
        const Comet::SceneSerializer serializer(components);
        auto active = std::make_unique<Comet::Scene>();
        auto entity = active->create_entity("Saved");
        const auto uuid = entity.get_uuid();
        entity.add_component<Comet::MeshRendererComponent>(mesh, material);
        active->create_entity("Shared").add_component<Comet::MeshRendererComponent>(mesh, material);
        const auto path = (Comet::ProjectPaths(root).assets() / "saved.scene").string();
        ASSERT_TRUE(serializer.save(*active, path));
        assets.reset();
        runtime.clear();
        std::ofstream(Comet::ProjectPaths(root).assets() / "model.gltf") << "invalid gltf";
        assets =
            std::make_unique<EditorAssets>(Comet::ProjectPaths(root), runtime, factory, scheduler);
        ASSERT_TRUE(assets->refresh().succeeded());
        CommandHistory history;
        SceneDocument document(
            serializer, Comet::ProjectPaths(root), history, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement) {
                EXPECT_EQ(assets->prepare_scene(*replacement, components).value(), 0);
                active.swap(replacement);
                return Comet::Result<void, Comet::Error>::success();
            });
        ASSERT_TRUE(document.open(path));
        EXPECT_TRUE(runtime.resolve<Comet::Mesh>(mesh));
        EXPECT_TRUE(runtime.resolve<Comet::Material>(material));
        EXPECT_EQ(
            active->find_entity(uuid).get_component<Comet::MeshRendererComponent>().mesh, mesh);
        EXPECT_EQ(factory.mesh_creation_count(), 1);
        assets->track_scene(*active, components);
        ASSERT_TRUE(assets->restore_references({100, std::chrono::seconds(1)}));
        EXPECT_EQ(assets->restore_references().value(), 0);
        ASSERT_TRUE(document.open(path));
        EXPECT_EQ(factory.mesh_creation_count(), 1);
    }

    TEST_F(EditorAssetsTest, MissingArtifactRepairsAfterPublicationWithoutChangingScene) {
        const auto components = Comet::create_scene_component_registry();
        const Comet::SceneSerializer serializer(components);
        auto active = std::make_unique<Comet::Scene>();
        active->create_entity("Missing").add_component<Comet::MeshRendererComponent>(
            mesh, Comet::AssetHandle(999));
        auto before_result = serializer.serialize(*active);
        ASSERT_TRUE(before_result) << before_result.error();
        auto before = std::move(before_result).value();
        const auto path = (Comet::ProjectPaths(root).assets() / "missing.scene").string();
        ASSERT_TRUE(serializer.save(*active, path));
        ASSERT_TRUE(assets->refresh().succeeded());
        CommandHistory history;
        SceneDocument document(
            serializer, Comet::ProjectPaths(root), history, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement) {
                EXPECT_EQ(assets->prepare_scene(*replacement, components).value(), 2);
                active.swap(replacement);
                return Comet::Result<void, Comet::Error>::success();
            });
        ASSERT_TRUE(document.open(path));
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
        EXPECT_EQ(factory.mesh_creation_count(), 0);
        complete_imports();
        EXPECT_FALSE(runtime.contains(mesh));
        assets->track_scene(*active, components);
        EXPECT_EQ(assets->restore_references({100, std::chrono::seconds(1)}).value(), 2);
        EXPECT_TRUE(runtime.contains(mesh));
        const auto serialized_again = serializer.serialize(*active);
        ASSERT_TRUE(serialized_again) << serialized_again.error();
        EXPECT_EQ(serialized_again.value(), before);
        for(int frame = 0; frame < 3; ++frame) {
            static_cast<void>(assets->update());
            EXPECT_EQ(assets->restore_references().value(), 0);
        }
        EXPECT_EQ(factory.mesh_creation_count(), 1);
    }

    TEST_F(EditorAssetsTest, SceneRefreshCoalescesSuccessfulScansButNotFailedImports) {
        const auto components = Comet::create_scene_component_registry();
        Comet::Scene scene;
        scene.create_entity().add_component<Comet::MeshRendererComponent>(
            mesh, Comet::AssetHandle{});
        std::ofstream(Comet::ProjectPaths(root).assets() / "model.gltf") << "invalid gltf";
        ASSERT_TRUE(assets->refresh().succeeded());
        ASSERT_TRUE(assets->refresh().succeeded());
        assets->track_scene(scene, components);
        EXPECT_EQ(assets->restore_references().value(), 1);
        EXPECT_EQ(assets->restore_references().value(), 0);
        complete_imports();
        EXPECT_EQ(assets->restore_references().value(), 0);
        std::filesystem::copy_file(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/meshes/cube.gltf",
            Comet::ProjectPaths(root).assets() / "model.gltf",
            std::filesystem::copy_options::overwrite_existing);
        ASSERT_TRUE(assets->refresh().succeeded());
        complete_imports();
        EXPECT_EQ(assets->restore_references().value(), 1);
        EXPECT_TRUE(runtime.contains(mesh));
    }

    TEST_F(EditorAssetsTest, SuccessfulTextureRepairRequestsSceneMaterialPreparation) {
        const auto directory = Comet::ProjectPaths(root).assets();
        std::filesystem::copy_file(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                       / "assets/textures/awesomeface.png",
            directory / "texture.png");
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto texture = assets->database().find("texture.png")->handle;
        EXPECT_TRUE(Comet::MaterialSerializer{}.save(
            {.template_name = "test", .texture_properties = {{"albedo", texture}}},
            directory / "textured.mat"));
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto material = assets->database().find("textured.mat")->handle;
        const auto components = Comet::create_scene_component_registry();
        Comet::Scene scene;
        scene.create_entity().add_component<Comet::MeshRendererComponent>(
            Comet::AssetHandle{}, material);
        EXPECT_EQ(assets->prepare_scene(scene, components).value(), 1);
        assets->track_scene(scene, components);
        EXPECT_EQ(assets->restore_references().value(), 1);
        EXPECT_FALSE(assets->apply_texture_edit(
            {{texture, assets->database().get_revision(texture)}, TextureEdit{}}));
        EXPECT_EQ(assets->restore_references().value(), 0);
        factory.fail_texture_creation(false);
        ASSERT_TRUE(assets->apply_texture_edit(
            {{texture, assets->database().get_revision(texture)}, TextureEdit{}}));
        EXPECT_FALSE(runtime.contains(material));
        EXPECT_EQ(assets->restore_references().value(), 1);
        EXPECT_TRUE(runtime.contains(material));
    }

    TEST_F(EditorAssetsTest, ExternalFileImportQueuesArtifactWithoutGpuOrExplicitRefresh) {
        const auto source = root / "external.gltf";
        std::filesystem::copy_file(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/meshes/cube.gltf",
            source);
        const std::array files{source};
        ASSERT_TRUE(assets->queue_import_files(files, {}));
        EXPECT_EQ(assets->database().find("external.gltf"), nullptr);
        ASSERT_TRUE(assets->update());
        scheduler.wait_idle();
        auto report = wait_for_source_report();
        ASSERT_TRUE(report);
        ASSERT_TRUE(report->succeeded());
        const auto* record = assets->database().find("external.gltf");
        ASSERT_NE(record, nullptr);
        const auto handle = record->handle;
        EXPECT_EQ(record->type, Comet::AssetType::Model);
        complete_imports();
        EXPECT_TRUE(assets->load_model({handle, assets->database().get_revision(handle)}));
        EXPECT_FALSE(runtime.contains(handle));
        EXPECT_EQ(factory.mesh_creation_count(), 0);
        EXPECT_TRUE(std::filesystem::exists(source));
    }

    TEST_F(EditorAssetsTest, ExternalFileImportQueueRejectsExcessRequests) {
        const auto source = root / "external.gltf";
        std::filesystem::copy_file(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/meshes/cube.gltf",
            source);
        const std::array files{source};
        for(int index = 0; index < 8; ++index)
            ASSERT_TRUE(assets->queue_import_files(files, {}));
        EXPECT_FALSE(assets->queue_import_files(files, {}));
        EXPECT_EQ(assets->database().find("external.gltf"), nullptr);

        Comet::AssetImportLimits limits;
        limits.external_file_queue = 2;
        EditorAssets smaller_queue(Comet::ProjectPaths(root), runtime, factory, scheduler,
            DEFAULT_FILE_CHANGE_QUIET_PERIOD, limits);
        ASSERT_TRUE(smaller_queue.queue_import_files(files, {}));
        ASSERT_TRUE(smaller_queue.queue_import_files(files, {}));
        EXPECT_FALSE(smaller_queue.queue_import_files(files, {}));
    }

    TEST_F(EditorAssetsTest, ExternalFileImportQueuePublishesRequestsInOrder) {
        const auto sample =
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/meshes/cube.gltf";
        const auto first = root / "first.gltf";
        const auto second = root / "second.gltf";
        std::filesystem::copy_file(sample, first);
        std::filesystem::copy_file(sample, second);
        const std::array first_files{first};
        const std::array second_files{second};
        ASSERT_TRUE(assets->queue_import_files(first_files, {}));
        ASSERT_TRUE(assets->queue_import_files(second_files, {}));

        for(int attempt = 0; attempt < 6 && !assets->database().find("second.gltf"); ++attempt) {
            ASSERT_TRUE(assets->update());
            scheduler.wait_idle();
        }
        ASSERT_TRUE(assets->update());
        EXPECT_TRUE(assets->database().find("first.gltf"));
        EXPECT_TRUE(assets->database().find("second.gltf"));
    }

    TEST_F(EditorAssetsTest, ReferenceLoadingDoesNotImportAndRejectsStaleRevisions) {
        const auto revision = assets->database().get_revision(mesh);
        EXPECT_TRUE(assets->load_reference({}, Comet::AssetType::Mesh, 0));
        EXPECT_FALSE(assets->load_reference(mesh, Comet::AssetType::Mesh, revision));
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
        EXPECT_EQ(factory.mesh_creation_count(), 0);
        complete_imports();
        EXPECT_FALSE(assets->load_reference(mesh, Comet::AssetType::Mesh, revision + 1));
        EXPECT_FALSE(assets->load_reference(mesh, Comet::AssetType::Material, revision));
        EXPECT_EQ(factory.mesh_creation_count(), 0);
        std::ofstream(Comet::ProjectPaths(root).assets() / "model.gltf") << "invalid";
        factory.fail_mesh_creation(true);
        EXPECT_FALSE(assets->load_reference(mesh, Comet::AssetType::Mesh, revision));
        EXPECT_FALSE(runtime.contains(mesh));
        factory.fail_mesh_creation(false);
        EXPECT_TRUE(assets->load_reference(mesh, Comet::AssetType::Mesh, revision));
        EXPECT_TRUE(assets->load_reference(mesh, Comet::AssetType::Mesh, revision));
        EXPECT_EQ(factory.mesh_creation_count(), 2);
    }

    TEST_F(EditorAssetsTest, MissingReferenceCannotBeLoadedButEmptyReferenceIsAllowed) {
        const auto missing = Comet::AssetHandle::generate();
        EXPECT_FALSE(assets->load_reference(
            missing, Comet::AssetType::Mesh, assets->database().get_revision(missing)));
        EXPECT_TRUE(assets->load_reference({}, Comet::AssetType::Mesh, {}));
        EXPECT_EQ(factory.mesh_creation_count(), 0);
    }

    TEST_F(EditorAssetsTest, ScannedMeshesImportAutomaticallyWithoutSelectionOrGpu) {
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
        complete_imports();
        EXPECT_TRUE(Comet::MeshArtifact::load(artifact_path(), mesh));
        EXPECT_FALSE(runtime.contains(mesh));
        EXPECT_EQ(factory.mesh_creation_count(), 0);
        // 无扫描事件时，不自动补建手工删除的缓存。
        std::filesystem::remove(artifact_path());
        complete_imports();
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
        ASSERT_TRUE(assets->refresh().succeeded());
        complete_imports();
        EXPECT_TRUE(Comet::MeshArtifact::load(artifact_path(), mesh));
    }

    TEST_F(EditorAssetsTest, UnrelatedAssetChangesDoNotRecheckImportedMeshes) {
        complete_imports();
        ASSERT_TRUE(Comet::MeshArtifact::load(artifact_path(), mesh));
        ASSERT_TRUE(std::filesystem::remove(artifact_path()));

        std::ofstream(Comet::ProjectPaths(root).assets() / "unrelated.lua") << "return {}\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
        const auto report = wait_for_source_report();
        ASSERT_TRUE(report);
        ASSERT_TRUE(report->succeeded());
        complete_imports();
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));

        ASSERT_TRUE(assets->refresh().succeeded());
        complete_imports();
        EXPECT_TRUE(Comet::MeshArtifact::load(artifact_path(), mesh));
    }

    TEST_F(EditorAssetsTest, AutomaticImportReusesCacheUntilExplicitReimport) {
        complete_imports();
        ASSERT_TRUE(assets->load_reference(
            mesh, Comet::AssetType::Mesh, assets->database().get_revision(mesh)));
        EXPECT_EQ(factory.mesh_creation_count(), 1);
        const auto stamp = std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
        std::filesystem::last_write_time(artifact_path(), stamp);
        ASSERT_TRUE(assets->refresh().succeeded());
        complete_imports();
        EXPECT_EQ(std::filesystem::last_write_time(artifact_path()), stamp);
        EXPECT_EQ(factory.mesh_creation_count(), 1);
        assets->request_source_reimport(mesh);
        complete_imports();
        EXPECT_EQ(factory.mesh_creation_count(), 2);
        EXPECT_NE(std::filesystem::last_write_time(artifact_path()), stamp);
    }

    TEST_F(EditorAssetsTest, SourceMonitorImportsNewAndModifiedMeshesWithoutRefresh) {
        complete_imports();
        const auto directory = Comet::ProjectPaths(root).assets();
        std::filesystem::copy_file(directory / "model.gltf", directory / "second.gltf");
        std::ofstream(directory / "model.gltf", std::ios::app) << "\n";
        const auto stamp = std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
        std::filesystem::last_write_time(artifact_path(), stamp);
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
        const auto report = wait_for_source_report();
        ASSERT_TRUE(report);
        ASSERT_TRUE(report->succeeded());
        complete_imports();
        const auto* added = assets->database().find("second.gltf");
        ASSERT_NE(added, nullptr);
        EXPECT_EQ(added->type, Comet::AssetType::Model);
        EXPECT_TRUE(
            assets->load_model({added->handle, assets->database().get_revision(added->handle)}));
        EXPECT_NE(std::filesystem::last_write_time(artifact_path()), stamp);
        EXPECT_EQ(factory.mesh_creation_count(), 0);
    }

    TEST_F(EditorAssetsTest, FailedImportWaitsForNewScanOrExplicitRetry) {
        const auto source = Comet::ProjectPaths(root).assets() / "model.gltf";
        std::ofstream(source, std::ios::trunc) << "invalid";
        ASSERT_TRUE(assets->refresh().succeeded());
        complete_imports();
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
        std::filesystem::copy_file(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/meshes/cube.gltf",
            source, std::filesystem::copy_options::overwrite_existing);
        ASSERT_TRUE(assets->refresh().succeeded());
        complete_imports();
        EXPECT_TRUE(Comet::MeshArtifact::load(artifact_path(), mesh));
        EXPECT_EQ(factory.mesh_creation_count(), 0);
    }

    TEST_F(EditorAssetsTest, AddingPreviouslyMissingBufferRetriesUnindexedDependency) {
        require_external_mesh_buffer();
        ASSERT_TRUE(assets->refresh().succeeded());
        complete_imports();
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
        EXPECT_TRUE(assets->database().get_import_dependencies(mesh).empty());
        add_external_mesh_buffer();
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
        const auto report = wait_for_source_report();
        ASSERT_TRUE(report);
        ASSERT_TRUE(report->succeeded());
        EXPECT_TRUE(report->added_assets.empty());
        EXPECT_TRUE(report->modified_assets.empty());
        complete_imports();
        EXPECT_TRUE(Comet::MeshArtifact::load(artifact_path(), mesh));
        EXPECT_FALSE(assets->database().get_import_dependencies(mesh).empty());
        EXPECT_EQ(factory.mesh_creation_count(), 0);
    }

    TEST_F(EditorAssetsTest, BufferAddedBeforeFailedCompletionStillRetriesMesh) {
        require_external_mesh_buffer();
        ASSERT_TRUE(assets->refresh().succeeded());
        ASSERT_TRUE(assets->update());
        scheduler.wait_idle();
        add_external_mesh_buffer();
        ASSERT_TRUE(assets->refresh().succeeded());

        ASSERT_TRUE(assets->update());
        complete_imports();
        EXPECT_TRUE(Comet::MeshArtifact::load(artifact_path(), mesh));
    }

    TEST_F(EditorAssetsTest, MovePreservesIdentityAndAcknowledgesEditorWrite) {
        ASSERT_TRUE(assets->move(mesh, "renamed.gltf").succeeded());
        EXPECT_EQ(assets->database().find("renamed.gltf")->handle, mesh);
        const auto report = assets->refresh();
        EXPECT_TRUE(report.added_assets.empty());
        EXPECT_TRUE(report.removed_assets.empty());
        EXPECT_TRUE(report.modified_assets.empty());
        complete_imports();
        const auto artifact = Comet::MeshArtifact::load(artifact_path(), mesh);
        ASSERT_TRUE(artifact);
        EXPECT_EQ(artifact->source_inputs.files.front().relative_path, "renamed.gltf");
    }
}

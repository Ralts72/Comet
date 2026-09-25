#include "render/resource/environment.h"
#include "assets/editor_assets.h"
#include "assets/material_editing.h"
#include "asset/serialization/metadata_serializer.h"
#include "asset/artifact/mesh_artifact.h"
#include "asset/registry.h"
#include "core/task_scheduler.h"
#include "core/project.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"
#include "render/material/material.h"
#include "scene/scene_document.h"
#include "scene/editor_scene_session.h"
#include "scene/scene_editor.h"
#include "scene/selection.h"
#include "editor_state.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"
#include "scene/scene_runtime.h"
#include "scene/script_component.h"
#include "scene/systems/script_system.h"
#include "scripting/script.h"
#include "common/file_io.h"
#include "asset/serialization/material_serializer.h"
#include "support/temporary_directory.h"
#include "support/render_resource_factory.h"
#include "support/hdr_image.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <array>
#include <fstream>
#include <optional>
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
        void SetUp() override {
            factory.fail_texture_creation(true);
            const auto directory = Comet::ProjectPaths(root).assets();
            std::filesystem::create_directories(directory);
            std::filesystem::copy_file(
                std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/meshes/cube.gltf",
                directory / "model.gltf");
            assets = std::make_unique<EditorAssets>(
                Comet::ProjectPaths(root), runtime, factory, scheduler);
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
            return SceneEditor::AssetInput{handle, assets->database().get_revision(handle),
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
        SceneEditor::AssetInput input{material, assets->database().get_revision(material),
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
        AssetRead request{handle, assets->database().get_revision(handle)};
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
            {handle, assets->database().get_revision(handle), MaterialEdit{data, unlit}});
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

    TEST_F(EditorAssetsTest, CreatesModuleScriptWithStableIdentityAndLoadsIt) {
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

    TEST_F(EditorAssetsTest, DeletingAssetMovesSourceAndIdentityToProjectTrash) {
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
        const auto trash = paths.local_data() / "trash";
        const auto entry = std::filesystem::directory_iterator(trash)->path();
        ASSERT_TRUE(std::filesystem::exists(entry / "remove_me.lua"));
        ASSERT_TRUE(std::filesystem::exists(entry / "remove_me.lua.meta"));
        std::filesystem::rename(entry / "remove_me.lua", paths.assets() / "remove_me.lua");
        std::filesystem::rename(
            entry / "remove_me.lua.meta", paths.assets() / "remove_me.lua.meta");
        ASSERT_TRUE(assets->refresh().succeeded());
        ASSERT_NE(assets->database().find(handle), nullptr);
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
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "keep.lua"));
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "keep.lua.meta"));
        EXPECT_NE(assets->database().find(script), nullptr);
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
        const AssetEdit edit{material, revision, MaterialEdit{original_data, data}};
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
        auto update = assets->prepare_material_edit({material,
            assets->database().get_revision(material), MaterialEdit{original_data, data}});
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
        const AssetEdit edit{material, assets->database().get_revision(material),
            MaterialEdit{{}, {.template_name = "updated"}}};
        auto update = assets->prepare_material_edit(edit);
        ASSERT_TRUE(update);
        ASSERT_TRUE(assets->commit_material_edit(update.value()));
        const auto published = runtime.resolve<Comet::Material>(material);
        ASSERT_TRUE(published);
        const auto revision = assets->database().get_revision(material);
        ASSERT_NE(revision, edit.revision);
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

    TEST_F(EditorAssetsTest, AutomaticMeshImportsSurviveDefaultQueueCapacity) {
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
            if(record.type != Comet::AssetType::Mesh)
                continue;
            EXPECT_TRUE(
                std::filesystem::exists(Comet::ProjectPaths(root).cache() / "imported/mesh"
                                        / (std::to_string(record.handle.value()) + ".bin")));
        }
        EXPECT_EQ(factory.mesh_creation_count(), 0);
    }

    TEST_F(EditorAssetsTest, RemovedAssetEditIsRejectedBeforePublication) {
        const auto material = add_material();
        const AssetEdit edit{material, assets->database().get_revision(material), MaterialEdit{}};
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
        assets->request_mesh_reimport(mesh);
        scheduler.wait_idle();

        const auto result = assets->update();
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code,
            (Comet::GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
        EXPECT_EQ(runtime.resolve<Comet::Mesh>(mesh), original);
    }

    TEST_F(EditorAssetsTest, DeviceLostDuringPreparationRejectsSceneInstallation) {
        complete_imports();
        const auto components = Comet::create_scene_component_registry();
        const Comet::SceneSerializer serializer(components);
        Comet::Scene candidate;
        candidate.create_entity().add_component<Comet::MeshRendererComponent>(
            mesh, Comet::AssetHandle{});
        ASSERT_TRUE(serializer.save(candidate, (root / "assets/candidate.scene").string()));
        auto active = std::make_unique<Comet::Scene>();
        const auto* original = active.get();
        CommandHistory history;
        SceneDocument document(
            serializer, Comet::ProjectPaths(root), history, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement) {
                const auto prepared = assets->prepare_scene(*replacement, components);
                if(!prepared)
                    return Comet::Result<void, Comet::Error>::failure(prepared.error());
                active.swap(replacement);
                return Comet::Result<void, Comet::Error>::success();
            });
        factory.fail_mesh_creation(true);
        factory.set_failure_result(vk::Result::eErrorDeviceLost);
        const auto loaded = assets->load_reference(
            mesh, Comet::AssetType::Mesh, assets->database().get_revision(mesh));
        ASSERT_FALSE(loaded);
        EXPECT_TRUE(Comet::is_device_lost(loaded.error()));
        const auto opened = document.open("candidate.scene");
        ASSERT_FALSE(opened);
        EXPECT_EQ(opened.error().code, loaded.error().code);
        EXPECT_EQ(active.get(), original);
        EXPECT_FALSE(runtime.contains(mesh));

        factory.set_failure_result(vk::Result::eErrorOutOfDeviceMemory);
        EXPECT_TRUE(document.open("candidate.scene"));
        EXPECT_NE(active.get(), original);
        EXPECT_FALSE(runtime.contains(mesh));
        const auto prepared = assets->prepare_scene(*active, components);
        ASSERT_TRUE(prepared);
        EXPECT_EQ(prepared.value(), 1U);
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
            {texture, assets->database().get_revision(texture), TextureEdit{}});
        ASSERT_FALSE(reimported);
        EXPECT_TRUE(Comet::is_device_lost(reimported.error()));
        const auto updated =
            assets->prepare_material_edit({material, assets->database().get_revision(material),
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
        const auto expected_references = components.collect_asset_references(*startup.value());
        const auto expected_meshes = std::ranges::count_if(expected_references,
            [](const auto& reference) { return reference.type == Comet::AssetType::Mesh; });
        ASSERT_GT(expected_meshes, 0);
        ASSERT_TRUE(document.open(project.startup_scene().string()));
        ASSERT_NE(active, nullptr);
        EXPECT_EQ(document.get_path(), path);
        // 项目 Shader 先完成后台编译，其材质和 Mesh 初次打开时都会暂缺。
        EXPECT_EQ(missing, expected_meshes + 1);
        EXPECT_EQ(factory.mesh_creation_count(), 0);
        EXPECT_EQ(active->entity_count(), initial_entities);
        const auto references = components.collect_asset_references(*active);
        EXPECT_EQ(references, expected_references);
        for(const auto& reference : references) {
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
        EXPECT_TRUE(document.get_last_error().empty());
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
            {texture, assets->database().get_revision(texture), TextureEdit{}}));
        EXPECT_EQ(assets->restore_references().value(), 0);
        factory.fail_texture_creation(false);
        ASSERT_TRUE(assets->apply_texture_edit(
            {texture, assets->database().get_revision(texture), TextureEdit{}}));
        EXPECT_FALSE(runtime.contains(material));
        EXPECT_EQ(assets->restore_references().value(), 1);
        EXPECT_TRUE(runtime.contains(material));
    }

    TEST_F(EditorAssetsTest, PlayPreparesCandidateAndStopRestoresWithoutReloading) {
        const auto components = Comet::create_scene_component_registry();
        const Comet::SceneSerializer serializer(components);
        auto active = std::make_unique<Comet::Scene>();
        active->create_entity().add_component<Comet::MeshRendererComponent>(
            mesh, Comet::AssetHandle{});
        complete_imports();
        EditorState state;
        int preparations = 0;
        Comet::SceneRuntime scene_runtime;
        const auto replace = [&](std::unique_ptr<Comet::Scene> replacement) {
            EXPECT_TRUE(scene_runtime.stop());
            active.swap(replacement);
            return replacement;
        };
        EditorSceneSession session(
            state, serializer, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> candidate) {
                ++preparations;
                EXPECT_EQ(assets->prepare_scene(*candidate, components).value(), 0);
                return Comet::Result<std::unique_ptr<Comet::Scene>, Comet::Error>::success(
                    replace(std::move(candidate)));
            },
            replace, [&] { return scene_runtime.start(*active); });
        session.request_mode(EditorMode::Play);
        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_TRUE(runtime.contains(mesh));
        const auto creations = factory.mesh_creation_count();
        session.request_mode(EditorMode::Edit);
        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_TRUE(runtime.contains(mesh));
        EXPECT_EQ(preparations, 1);
        EXPECT_EQ(factory.mesh_creation_count(), creations);
    }

    TEST_F(EditorAssetsTest, ExternalFileImportQueuesArtifactWithoutGpuOrExplicitRefresh) {
        const auto source = root / "external.gltf";
        std::filesystem::copy_file(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/meshes/cube.gltf",
            source);
        const std::array files{source};
        const auto report = assets->import_files(files, {});
        ASSERT_TRUE(report.succeeded());
        const auto* record = assets->database().find("external.gltf");
        ASSERT_NE(record, nullptr);
        const auto handle = record->handle;
        const auto artifact =
            artifact_path().parent_path() / (std::to_string(handle.value()) + ".bin");
        EXPECT_FALSE(std::filesystem::exists(artifact));
        complete_imports();
        EXPECT_TRUE(Comet::MeshArtifact::load(artifact, handle));
        EXPECT_FALSE(runtime.contains(handle));
        EXPECT_EQ(factory.mesh_creation_count(), 0);
        EXPECT_TRUE(std::filesystem::exists(source));
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
        assets->request_mesh_reimport(mesh);
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
        const auto second_artifact =
            artifact_path().parent_path() / (std::to_string(added->handle.value()) + ".bin");
        EXPECT_TRUE(Comet::MeshArtifact::load(second_artifact, added->handle));
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

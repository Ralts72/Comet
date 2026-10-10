#include "asset/project_prepare.h"
#include "asset/shader_program_import.h"
#include "asset/import/import_service.h"
#include "asset/serialization/metadata_serializer.h"
#include "common/file_io.h"
#include "core/project.h"
#include "graphics/pipeline/shader_interface.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "scene/script_component.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    class ProjectPrepareTest: public testing::Test {
    protected:
        TemporaryDirectory directory;
        ProjectPaths paths{directory.path()};
        ComponentRegistry components = create_scene_component_registry();
        AssetHandle mesh;
        AssetHandle program;

        void SetUp() override {
            const std::filesystem::path demo = COMET_SAMPLE_PROJECT_DIRECTORY;
            for(const auto* relative : {"meshes/cube.gltf", "materials/stripes.mat",
                    "shaders/stripes.shader", "shaders/stripes.vert", "shaders/stripes.frag"}) {
                const auto destination = paths.assets() / relative;
                std::filesystem::create_directories(destination.parent_path());
                std::filesystem::copy_file(demo / "assets" / relative, destination);
                std::filesystem::copy_file(
                    metadata_path(demo / "assets" / relative), metadata_path(destination));
            }
            AssetDatabase database(paths);
            ASSERT_TRUE(database.scan().succeeded());
            const auto* mesh_record = database.find("meshes/cube.gltf");
            const auto* material_record = database.find("materials/stripes.mat");
            const auto* program_record = database.find("shaders/stripes.shader");
            ASSERT_NE(mesh_record, nullptr);
            ASSERT_NE(material_record, nullptr);
            ASSERT_NE(program_record, nullptr);
            mesh = mesh_record->handle;
            program = program_record->handle;
            Scene scene;
            scene.create_entity("Cube").add_component<MeshRendererComponent>(
                MeshRendererComponent{.mesh = mesh, .material = material_record->handle});
            ASSERT_TRUE(SceneSerializer(components).save(scene, paths.assets() / "startup.scene"));
            ASSERT_TRUE(write_text_file_atomic(paths.root() / "project.json", R"({
                "version":2,"id":"00000000-0000-4000-8000-000000000001",
                "name":"CPU asset test","startup_scene":"startup.scene",
                "input_contexts":[],"input_actions":[]})"));
        }
    };

    TEST_F(ProjectPrepareTest, PreparesMeshAndShaderDependenciesAndReusesTheirArtifacts) {
        auto project = Project::load(paths.root());
        ASSERT_TRUE(project) << project.error();
        auto prepared = prepare_project(project.value());
        ASSERT_TRUE(prepared) << prepared.error();
        ImportService imports(paths);
        const auto mesh_path = imports.mesh_artifact_path(mesh);
        const auto shader_path = imports.shader_program_artifact_path(program);
        auto mesh_artifact =
            MeshArtifact::load(mesh_path, mesh, AssetImportLimits{}.mesh_working_bytes);
        auto shader_artifact = ShaderProgramArtifact::load(shader_path, program);
        ASSERT_TRUE(mesh_artifact);
        EXPECT_EQ(mesh_artifact->data.vertices.size(), 24);
        EXPECT_EQ(mesh_artifact->data.indices.size(), 36);
        ASSERT_TRUE(shader_artifact);
        EXPECT_FALSE(shader_artifact->vertex_words.empty());
        EXPECT_FALSE(shader_artifact->fragment_words.empty());
        const auto vertex = ShaderInterface::reflect(shader_artifact->vertex_words);
        const auto fragment = ShaderInterface::reflect(shader_artifact->fragment_words);
        ASSERT_TRUE(vertex) << vertex.error();
        ASSERT_TRUE(fragment) << fragment.error();
        EXPECT_EQ(vertex.value().get_stage(), ShaderStage::Vertex);
        EXPECT_EQ(fragment.value().get_stage(), ShaderStage::Fragment);
        EXPECT_TRUE(vertex.value().validate_stage_link(fragment.value()));
        const auto mesh_stamp = std::filesystem::last_write_time(mesh_path);
        const auto shader_stamp = std::filesystem::last_write_time(shader_path);
        ASSERT_TRUE(prepare_project(project.value()));
        EXPECT_EQ(std::filesystem::last_write_time(mesh_path), mesh_stamp);
        EXPECT_EQ(std::filesystem::last_write_time(shader_path), shader_stamp);
        const auto repeated = ShaderProgramArtifact::load(shader_path, program);
        ASSERT_TRUE(repeated);
        EXPECT_EQ(repeated->vertex_words, shader_artifact->vertex_words);
        EXPECT_EQ(repeated->fragment_words, shader_artifact->fragment_words);
    }

    TEST_F(ProjectPrepareTest, InvalidSourcePreservesPublishedShaderArtifact) {
        auto project = Project::load(paths.root());
        ASSERT_TRUE(project);
        ASSERT_TRUE(prepare_project(project.value()));
        const auto artifact_path = ImportService(paths).shader_program_artifact_path(program);
        const auto before = read_text_file(artifact_path);
        ASSERT_TRUE(before);
        AssetDatabase database(paths);
        ASSERT_TRUE(database.scan().succeeded());
        const auto request = ShaderProgramImport::resolve(database, program);
        ASSERT_TRUE(request);
        ASSERT_TRUE(write_text_file_atomic(request.value().vertex.path, "invalid shader"));
        EXPECT_FALSE(prepare_project(project.value()));
        const auto after = read_text_file(artifact_path);
        ASSERT_TRUE(after);
        EXPECT_EQ(after.value(), before.value());
    }

    TEST_F(ProjectPrepareTest, MissingRequiredAssetFailsWithItsHandle) {
        auto project = Project::load(paths.root());
        ASSERT_TRUE(project);
        std::filesystem::remove(paths.assets() / "meshes/cube.gltf");
        auto prepared = prepare_project(project.value());
        ASSERT_FALSE(prepared);
        EXPECT_NE(prepared.error().find(std::to_string(mesh.value())), std::string::npos);
    }

    TEST_F(ProjectPrepareTest, ChecksUiReferencesWithoutAWindowOrGraphicsDevice) {
        ASSERT_TRUE(write_text_file_atomic(paths.root() / "project.json", R"({
            "version":2,"id":"00000000-0000-4000-8000-000000000001",
            "name":"UI preparation","startup_scene":"startup.scene",
            "input_contexts":[],"input_actions":[],
            "ui":{"document":"ui/menu.rml","controller":"ui/menu.ui.lua"}})"));
        ASSERT_TRUE(write_text_file_atomic(paths.assets() / "ui/menu.ui.lua", "return {}"));
        ASSERT_TRUE(write_text_file_atomic(paths.assets() / "ui/menu.rml", R"(
            <rml><body><div style="display: none"><img src="hidden.png" /></div></body></rml>)"));
        const auto project = Project::load(paths.root());
        ASSERT_TRUE(project);
        const auto prepared = prepare_project(project.value());
        ASSERT_FALSE(prepared);
        EXPECT_NE(prepared.error().find("hidden.png"), std::string::npos);
        ASSERT_TRUE(write_text_file_atomic(paths.assets() / "ui/hidden.png", "image bytes"));
        ASSERT_TRUE(prepare_project(project.value()));
    }

    TEST(ProjectContentTest, PreservesPersistentSystemComponentsWithoutTheirBackends) {
        const auto components = create_scene_component_registry();
        const SceneSerializer serializer(components);
        Scene scene;
        auto actor = scene.create_entity("Actor");
        actor.add_component<ScriptComponent>().asset = AssetHandle(101);
        actor.get_component<ScriptComponent>().parameters = {{"speed", 2.0f}};
        actor.add_component<AudioSourceComponent>().clip = AssetHandle(102);
        actor.get_component<AudioSourceComponent>().loop = true;
        actor.add_component<RigidBodyComponent>().mass = 3.0f;
        actor.add_component<ColliderComponent>().radius = 0.75f;
        const auto serialized = serializer.serialize(scene);
        ASSERT_TRUE(serialized) << serialized.error();
        const auto loaded = serializer.deserialize(serialized.value());
        ASSERT_TRUE(loaded) << loaded.error();
        const auto restored = loaded.value()->find_entity(actor.get_uuid());
        ASSERT_TRUE(restored);
        ASSERT_TRUE(restored.has_component<ScriptComponent>());
        ASSERT_TRUE(restored.has_component<AudioSourceComponent>());
        ASSERT_TRUE(restored.has_component<RigidBodyComponent>());
        ASSERT_TRUE(restored.has_component<ColliderComponent>());
        EXPECT_EQ(restored.get_component<ScriptComponent>().asset, AssetHandle(101));
        EXPECT_EQ(restored.get_component<ScriptComponent>().parameters,
            actor.get_component<ScriptComponent>().parameters);
        EXPECT_EQ(restored.get_component<AudioSourceComponent>().clip, AssetHandle(102));
        EXPECT_TRUE(restored.get_component<AudioSourceComponent>().loop);
        EXPECT_FLOAT_EQ(restored.get_component<RigidBodyComponent>().mass, 3.0f);
        EXPECT_FLOAT_EQ(restored.get_component<ColliderComponent>().radius, 0.75f);
    }
}

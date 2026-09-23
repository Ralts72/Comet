#include "assets/shader_program_import.h"
#include "assets/editor_assets.h"
#include "asset/registry.h"
#include "asset/import/import_service.h"
#include "asset/serialization/shader_program_serializer.h"
#include "asset/serialization/material_serializer.h"
#include "common/file_io.h"
#include "core/task_scheduler.h"
#include "graphics/pipeline/shader_interface.h"
#include "render/material/material.h"
#include "render/material/material_layout.h"
#include "render/material/material_shader.h"
#include "support/render_resource_factory.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>

namespace CometEditor::Tests {
    namespace {
        constexpr std::string_view VERTEX = "#version 450\nlayout(location=0) in vec3 position;\n"
                                            "void main(){gl_Position=vec4(position,1.0);}\n";
        constexpr std::string_view FRAGMENT =
            "#version 450\n#extension GL_GOOGLE_include_directive : require\n"
            "#include \"color.glsl\"\nlayout(location=0) out vec4 out_color;\n"
            "void main(){out_color=COLOR;}\n";

        class ShaderProgramImportTest: public testing::Test {
        protected:
            Comet::Tests::TemporaryDirectory directory;
            Comet::ProjectPaths paths{directory.path()};
            Comet::AssetDatabase database{paths};
            Comet::AssetHandle program;

            void SetUp() override {
                std::filesystem::create_directories(paths.assets() / "shaders");
                ASSERT_TRUE(
                    Comet::write_text_file_atomic(paths.assets() / "shaders/test.vert", VERTEX));
                ASSERT_TRUE(
                    Comet::write_text_file_atomic(paths.assets() / "shaders/test.frag", FRAGMENT));
                ASSERT_TRUE(Comet::write_text_file_atomic(paths.assets() / "shaders/color.glsl",
                    "#define COLOR vec4(1.0,0.0,0.0,1.0)\n"));
                ASSERT_TRUE(database.scan().succeeded());
                const auto* vertex = database.find("shaders/test.vert");
                const auto* fragment = database.find("shaders/test.frag");
                ASSERT_NE(vertex, nullptr);
                ASSERT_NE(fragment, nullptr);
                Comet::ShaderProgramData data{{vertex->handle, "main"}, {fragment->handle, "main"}};
                ASSERT_TRUE(Comet::ShaderProgramSerializer{}.save(
                    data, paths.assets() / "shaders/test.shader"));
                ASSERT_TRUE(database.scan().succeeded());
                const auto* record = database.find("shaders/test.shader");
                ASSERT_NE(record, nullptr);
                program = record->handle;
            }

            ShaderProgramImport::Request request() {
                auto result = ShaderProgramImport::resolve(database, paths, program);
                EXPECT_TRUE(result) << result.error();
                return std::move(result).value();
            }

            std::filesystem::path artifact_path() const {
                return Comet::ImportService(paths).shader_program_artifact_path(program);
            }

            std::shared_ptr<const Comet::ShaderProgramArtifact> publish_with_editor() {
                Comet::Tests::FakeRenderResourceFactory factory;
                Comet::AssetRegistry registry;
                Comet::TaskScheduler scheduler(1);
                EditorAssets assets(paths, registry, factory, scheduler);
                EXPECT_TRUE(assets.refresh().succeeded());
                EXPECT_TRUE(assets.update());
                scheduler.wait_idle();
                EXPECT_TRUE(assets.update());
                return assets.compiled_shader_program(program);
            }
        };
    }

    TEST(ShaderProgramSerializerTest, RejectsUnsupportedVersionAndInvalidStageIdentity) {
        const Comet::ShaderProgramSerializer serializer;
        EXPECT_FALSE(serializer.deserialize(
            R"({"version":2,"vertex":{"source":1,"entry":"main"},"fragment":{"source":2,"entry":"main"}})"));
        EXPECT_FALSE(serializer.deserialize(
            R"({"version":1,"vertex":{"source":0,"entry":"main"},"fragment":{"source":2,"entry":"main"}})"));
    }

    TEST(ShaderProgramSerializerTest, RejectsInvalidMaterialPropertyMetadata) {
        const Comet::ShaderProgramSerializer serializer;
        EXPECT_FALSE(serializer.deserialize_material(
            R"({"material":{"textures":[{"name":"value"}],"scalars":[{"name":"value","default":1}]}})"));
        EXPECT_FALSE(serializer.deserialize_material(
            R"({"material":{"scalars":[{"name":"value","default":1,"min":2,"max":1}]}})"));
        EXPECT_FALSE(serializer.deserialize_material(
            R"({"material":{"vectors":[{"name":"tint","default":[1,2,3]}]}})"));
        EXPECT_FALSE(serializer.deserialize_material(R"({"material":{},"unexpected":1})"));
    }

    TEST(DemoShaderProgramTest, CompilesForUnlitMaterial) {
        const Comet::ProjectPaths paths(COMET_SAMPLE_PROJECT_DIRECTORY);
        Comet::AssetDatabase database(paths);
        ASSERT_TRUE(database.scan().snapshot_updated);
        const auto* program = database.find("shaders/stripes.shader");
        const auto* material = database.find("materials/stripes.mat");
        ASSERT_NE(program, nullptr);
        ASSERT_NE(material, nullptr);

        auto data = Comet::MaterialSerializer{}.load(paths.assets() / material->path);
        ASSERT_TRUE(data) << data.error();
        EXPECT_EQ(data.value().template_name, "unlit_color");
        EXPECT_EQ(data.value().shader_program, program->handle);

        auto request = ShaderProgramImport::resolve(database, paths, program->handle);
        ASSERT_TRUE(request) << request.error();
        auto prepared = ShaderProgramImport::prepare(paths, request.value());
        ASSERT_TRUE(prepared) << prepared.error().message;
        const auto& artifact = prepared.value().artifact;
        EXPECT_TRUE(Comet::validate_material_shaders(
            {{"unlit_color", {artifact.vertex_words, artifact.fragment_words, artifact.vertex_entry,
                                 artifact.fragment_entry}}}));
        auto shader =
            Comet::ShaderInterface::reflect(artifact.fragment_words, artifact.fragment_entry);
        ASSERT_TRUE(shader) << shader.error();
        ASSERT_TRUE(artifact.material);
        auto reflected =
            Comet::MaterialLayout::from_program("unlit_color", *artifact.material, shader.value());
        ASSERT_TRUE(reflected) << reflected.error();
        EXPECT_EQ(reflected.value()->get_scalars().size(), 2u);
        EXPECT_EQ(reflected.value()->get_scalars()[1].name, "frequency");
        EXPECT_EQ(reflected.value()->get_scalars()[1].default_value, 18.0f);
        Comet::Tests::TemporaryDirectory output;
        const auto cache = output.path() / "stripes.csp";
        ASSERT_TRUE(artifact.publish_atomic(cache));
        const auto restored = Comet::ShaderProgramArtifact::load(cache, program->handle);
        ASSERT_TRUE(restored);
        ASSERT_TRUE(restored->material);
        EXPECT_EQ(restored->material, artifact.material);
    }

    TEST_F(ShaderProgramImportTest, StableIdentityAndSourceDependencies) {
        const auto* record = database.find(program);
        ASSERT_NE(record, nullptr);
        ASSERT_EQ(record->dependencies.size(), 2u);
        EXPECT_EQ(record->type, Comet::AssetType::ShaderProgram);
        EXPECT_EQ(database.find("shaders/color.glsl"), nullptr);
        const auto before = request();
        std::filesystem::rename(
            paths.assets() / "shaders/test.shader", paths.assets() / "shaders/renamed.shader");
        std::filesystem::rename(paths.assets() / "shaders/test.shader.meta",
            paths.assets() / "shaders/renamed.shader.meta");
        ASSERT_TRUE(database.scan().succeeded());
        EXPECT_EQ(database.find(program)->path, "shaders/renamed.shader");
        EXPECT_NE(database.get_revision(program), before.revision);
    }

    TEST_F(ShaderProgramImportTest, SourceEditsAdvanceProgramRevision) {
        const auto before = request();
        ASSERT_TRUE(Comet::write_text_file_atomic(
            paths.assets() / "shaders/test.frag", std::string(FRAGMENT) + "\n// changed\n"));
        const auto report = database.scan();
        ASSERT_TRUE(report.succeeded());
        EXPECT_NE(database.get_revision(program), before.revision);
        EXPECT_NE(std::ranges::find(report.modified_assets, program), report.modified_assets.end());
    }

    TEST_F(ShaderProgramImportTest, ImportDependencyUpdateKeepsProgramRevisionStableOnRescan) {
        ASSERT_TRUE(database.update_import_dependencies(program, {"shaders/color.glsl"}));
        const auto revision = database.get_revision(program);
        const auto report = database.scan();
        ASSERT_TRUE(report.succeeded());
        EXPECT_TRUE(report.modified_assets.empty());
        EXPECT_EQ(database.get_revision(program), revision);
    }

    TEST_F(ShaderProgramImportTest, PublishesImmutableCpuVersionAndReusesValidatedCache) {
        const auto published = publish_with_editor();
        ASSERT_TRUE(published);
        ASSERT_FALSE(published->vertex_words.empty());
        EXPECT_FALSE(published->fragment_words.empty());
        EXPECT_EQ(published->vertex_entry, "main");
        EXPECT_EQ(published->fragment_entry, "main");
        EXPECT_TRUE(std::filesystem::exists(artifact_path()));
        auto cached = ShaderProgramImport::prepare(paths, request());
        ASSERT_TRUE(cached) << cached.error().message;
        EXPECT_TRUE(cached.value().from_cache);
        EXPECT_EQ(cached.value().artifact.vertex_words, published->vertex_words);
        EXPECT_EQ(cached.value().artifact.vertex_entry, published->vertex_entry);
        EXPECT_EQ(cached.value().artifact.fragment_entry, published->fragment_entry);
    }

    TEST_F(ShaderProgramImportTest, CorruptCacheRebuildsWithoutChangingIdentity) {
        ASSERT_TRUE(publish_with_editor());
        const auto path = artifact_path();
        std::ifstream input(path, std::ios::binary);
        std::vector<char> bytes(
            std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
        ASSERT_GT(bytes.size(), 64u);
        bytes.back() ^= 1;
        ASSERT_TRUE(Comet::write_binary_file_atomic(path, std::as_bytes(std::span(bytes))));
        auto rebuilt = ShaderProgramImport::prepare(paths, request());
        ASSERT_TRUE(rebuilt) << rebuilt.error().message;
        EXPECT_FALSE(rebuilt.value().from_cache);
        EXPECT_EQ(rebuilt.value().artifact.handle, program);
    }

    TEST_F(ShaderProgramImportTest, MaterialLoadsPublishedProgramByStableHandle) {
        ASSERT_TRUE(publish_with_editor());
        std::filesystem::create_directories(paths.assets() / "materials");
        ASSERT_TRUE(
            Comet::MaterialSerializer{}.save({.template_name = "pbr", .shader_program = program},
                paths.assets() / "materials/project.mat"));
        Comet::Tests::FakeRenderResourceFactory factory;
        Comet::AssetRegistry registry;
        Comet::TaskScheduler scheduler(1);
        Comet::AssetManager manager(paths, registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto* record = manager.get_database().find("materials/project.mat");
        ASSERT_NE(record, nullptr);
        ASSERT_EQ(record->dependencies, std::vector{program});
        auto material = manager.load_material(record->handle);
        ASSERT_TRUE(material) << material.error().message;
        EXPECT_EQ(material.value()->get_shader_program(), program);
        EXPECT_TRUE(manager.compiled_shader_program(program));
    }

    TEST_F(ShaderProgramImportTest, FailedCompilationDoesNotReplacePublishedArtifact) {
        ASSERT_TRUE(publish_with_editor());
        const auto original = Comet::ShaderProgramArtifact::load(artifact_path(), program);
        ASSERT_TRUE(original);
        ASSERT_TRUE(Comet::write_text_file_atomic(
            paths.assets() / "shaders/test.frag", "#version 450\ninvalid source\n"));
        ASSERT_TRUE(database.scan().succeeded());
        auto failed = ShaderProgramImport::prepare(paths, request());
        EXPECT_FALSE(failed);
        const auto retained = Comet::ShaderProgramArtifact::load(artifact_path(), program);
        ASSERT_TRUE(retained);
        EXPECT_EQ(retained->fragment_words, original->fragment_words);
    }

    TEST_F(ShaderProgramImportTest, RejectsStaleCandidateAfterIncludeChanges) {
        Comet::Tests::FakeRenderResourceFactory factory;
        Comet::AssetRegistry registry;
        Comet::TaskScheduler scheduler(1);
        Comet::AssetManager manager(paths, registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        auto resolved = ShaderProgramImport::resolve(manager.get_database(), paths, program);
        ASSERT_TRUE(resolved);
        auto prepared = ShaderProgramImport::prepare(paths, resolved.value());
        ASSERT_TRUE(prepared) << prepared.error().message;
        ASSERT_TRUE(Comet::write_text_file_atomic(
            paths.assets() / "shaders/color.glsl", "#define COLOR vec4(0.0,1.0,0.0,1.0)\n"));
        ASSERT_TRUE(
            manager.import_shader_program_async(resolved.value(), [prepared] { return prepared; }));
        EXPECT_EQ(manager.get_async_status().in_flight, 1u);
        scheduler.wait_idle();
        ASSERT_TRUE(manager.process_completions());
        EXPECT_FALSE(manager.compiled_shader_program(program));
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
    }

    TEST_F(ShaderProgramImportTest, RejectsStaleCandidateAfterDatabaseRevisionChanges) {
        Comet::Tests::FakeRenderResourceFactory factory;
        Comet::AssetRegistry registry;
        Comet::TaskScheduler scheduler(1);
        Comet::AssetManager manager(paths, registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        auto resolved = ShaderProgramImport::resolve(manager.get_database(), paths, program);
        ASSERT_TRUE(resolved);
        auto prepared = ShaderProgramImport::prepare(paths, resolved.value());
        ASSERT_TRUE(prepared) << prepared.error().message;
        ASSERT_TRUE(Comet::write_text_file_atomic(
            paths.assets() / "shaders/test.vert", std::string(VERTEX) + "\n"));
        ASSERT_TRUE(manager.scan().succeeded());
        if(manager.import_shader_program_async(resolved.value(), [prepared] { return prepared; })) {
            scheduler.wait_idle();
            ASSERT_TRUE(manager.process_completions());
        }
        EXPECT_FALSE(manager.compiled_shader_program(program));
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
    }

    TEST_F(ShaderProgramImportTest, UnexpectedWorkerExceptionPropagatesAndReleasesSharedQueueSlot) {
        Comet::Tests::FakeRenderResourceFactory factory;
        Comet::AssetRegistry registry;
        Comet::TaskScheduler scheduler(1);
        Comet::AssetManager manager(paths, registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        auto resolved = ShaderProgramImport::resolve(manager.get_database(), paths, program);
        ASSERT_TRUE(resolved);
        const auto input = resolved.value();
        using Preparation =
            Comet::Result<Comet::ShaderProgramImportPrepared, Comet::ShaderProgramImportFailure>;
        ASSERT_TRUE(manager.import_shader_program_async(
            input, []() -> Preparation { throw std::runtime_error("worker failed"); }));
        scheduler.wait_idle();
        EXPECT_THROW(
            { [[maybe_unused]] const auto completion = manager.process_completions(); },
            std::runtime_error);
        EXPECT_EQ(manager.get_async_status().in_flight, 0u);
        EXPECT_FALSE(manager.compiled_shader_program(program));

        ASSERT_TRUE(manager.import_shader_program_async(
            input, [paths = paths, input] { return ShaderProgramImport::prepare(paths, input); }));
        scheduler.wait_idle();
        ASSERT_TRUE(manager.process_completions());
        EXPECT_TRUE(manager.compiled_shader_program(program));
    }

    TEST_F(ShaderProgramImportTest, EditorPublishesAndTracksIncludeChangesWithoutGpuObjects) {
        Comet::Tests::FakeRenderResourceFactory factory;
        Comet::AssetRegistry registry;
        Comet::TaskScheduler scheduler(1);
        EditorAssets assets(paths, registry, factory, scheduler);
        ASSERT_TRUE(assets.refresh().succeeded());
        ASSERT_TRUE(assets.update());
        scheduler.wait_idle();
        ASSERT_TRUE(assets.update());
        const auto first = assets.compiled_shader_program(program);
        ASSERT_TRUE(first);
        EXPECT_FALSE(assets.database().get_import_dependencies(program).empty());

        ASSERT_TRUE(Comet::write_text_file_atomic(
            paths.assets() / "shaders/color.glsl", "#define COLOR vec4(0.0,1.0,0.0,1.0)\n"));
        ASSERT_TRUE(assets.refresh().succeeded());
        ASSERT_TRUE(assets.update());
        scheduler.wait_idle();
        ASSERT_TRUE(assets.update());
        const auto second = assets.compiled_shader_program(program);
        ASSERT_TRUE(second);
        EXPECT_NE(second, first);
        EXPECT_NE(second->fragment_words, first->fragment_words);

        ASSERT_TRUE(Comet::write_text_file_atomic(
            paths.assets() / "shaders/test.frag", "#version 450\ninvalid source\n"));
        ASSERT_TRUE(assets.refresh().succeeded());
        ASSERT_TRUE(assets.update());
        scheduler.wait_idle();
        ASSERT_TRUE(assets.update());
        EXPECT_EQ(assets.compiled_shader_program(program), second);
    }

    TEST_F(ShaderProgramImportTest, MissingIncludeCreationTriggersRetry) {
        Comet::Tests::FakeRenderResourceFactory factory;
        Comet::AssetRegistry registry;
        Comet::TaskScheduler scheduler(1);
        EditorAssets assets(paths, registry, factory, scheduler);
        ASSERT_TRUE(assets.refresh().succeeded());
        ASSERT_TRUE(assets.update());
        scheduler.wait_idle();
        ASSERT_TRUE(assets.update());
        const auto first = assets.compiled_shader_program(program);
        ASSERT_TRUE(first);

        ASSERT_TRUE(Comet::write_text_file_atomic(paths.assets() / "shaders/test.frag",
            "#version 450\n#extension GL_GOOGLE_include_directive : require\n"
            "#include \"new_color.glsl\"\nlayout(location=0) out vec4 out_color;\n"
            "void main(){out_color=NEW_COLOR;}\n"));
        ASSERT_TRUE(assets.refresh().succeeded());
        ASSERT_TRUE(assets.update());
        scheduler.wait_idle();
        ASSERT_TRUE(assets.update());
        EXPECT_EQ(assets.compiled_shader_program(program), first);

        ASSERT_TRUE(Comet::write_text_file_atomic(paths.assets() / "shaders/new_color.glsl",
            "#define NEW_COLOR vec4(0.2,0.3,0.4,1.0)\n"));
        const auto report = assets.refresh();
        ASSERT_TRUE(report.succeeded());
        EXPECT_NE(std::ranges::find(report.modified_assets, program), report.modified_assets.end());
        ASSERT_TRUE(assets.update());
        scheduler.wait_idle();
        ASSERT_TRUE(assets.update());
        EXPECT_NE(assets.compiled_shader_program(program), first);
    }

    TEST_F(ShaderProgramImportTest, MissingSourceRetainsVersionUntilProgramIsRemoved) {
        Comet::Tests::FakeRenderResourceFactory factory;
        Comet::AssetRegistry registry;
        Comet::TaskScheduler scheduler(1);
        EditorAssets assets(paths, registry, factory, scheduler);
        ASSERT_TRUE(assets.refresh().succeeded());
        ASSERT_TRUE(assets.update());
        scheduler.wait_idle();
        ASSERT_TRUE(assets.update());
        const auto first = assets.compiled_shader_program(program);
        ASSERT_TRUE(first);

        ASSERT_TRUE(std::filesystem::remove(paths.assets() / "shaders/test.frag"));
        ASSERT_TRUE(assets.refresh().snapshot_updated);
        ASSERT_TRUE(assets.update());
        EXPECT_EQ(assets.compiled_shader_program(program), first);

        ASSERT_TRUE(std::filesystem::remove(paths.assets() / "shaders/test.shader"));
        ASSERT_TRUE(assets.refresh().snapshot_updated);
        ASSERT_TRUE(assets.update());
        EXPECT_FALSE(assets.compiled_shader_program(program));
    }
}

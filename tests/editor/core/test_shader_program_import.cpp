#include "assets/shader_program_import.h"
#include "assets/editor_assets.h"
#include "asset/registry.h"
#include "asset/serialization/shader_program_serializer.h"
#include "common/file_io.h"
#include "core/task_scheduler.h"
#include "support/render_resource_factory.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <span>

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
        };
    }

    TEST(ShaderProgramSerializerTest, RejectsUnsupportedVersionAndInvalidStageIdentity) {
        const Comet::ShaderProgramSerializer serializer;
        EXPECT_FALSE(serializer.deserialize(
            R"({"version":2,"vertex":{"source":1,"entry":"main"},"fragment":{"source":2,"entry":"main"}})"));
        EXPECT_FALSE(serializer.deserialize(
            R"({"version":1,"vertex":{"source":0,"entry":"main"},"fragment":{"source":2,"entry":"main"}})"));
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

    TEST_F(ShaderProgramImportTest, PublishesImmutableCpuVersionAndReusesValidatedCache) {
        const auto resolved = request();
        auto prepared = ShaderProgramImport::prepare(paths, resolved);
        ASSERT_TRUE(prepared) << prepared.error().message;
        EXPECT_FALSE(prepared.value().from_cache);
        auto published =
            ShaderProgramImport::publish(database, paths, resolved, std::move(prepared).value());
        ASSERT_TRUE(published) << published.error();
        ASSERT_FALSE(published.value()->vertex_words.empty());
        EXPECT_FALSE(published.value()->fragment_words.empty());
        EXPECT_TRUE(std::filesystem::exists(ShaderProgramImport::artifact_path(paths, program)));
        auto cached = ShaderProgramImport::prepare(paths, request());
        ASSERT_TRUE(cached) << cached.error().message;
        EXPECT_TRUE(cached.value().from_cache);
        EXPECT_EQ(cached.value().artifact.vertex_words, published.value()->vertex_words);
    }

    TEST_F(ShaderProgramImportTest, CorruptCacheRebuildsWithoutChangingIdentity) {
        const auto resolved = request();
        auto first = ShaderProgramImport::prepare(paths, resolved);
        ASSERT_TRUE(first) << first.error().message;
        ASSERT_TRUE(
            ShaderProgramImport::publish(database, paths, resolved, std::move(first).value()));
        const auto path = ShaderProgramImport::artifact_path(paths, program);
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

    TEST_F(ShaderProgramImportTest, FailedCompilationDoesNotReplacePublishedArtifact) {
        const auto resolved = request();
        auto first = ShaderProgramImport::prepare(paths, resolved);
        ASSERT_TRUE(first) << first.error().message;
        ASSERT_TRUE(
            ShaderProgramImport::publish(database, paths, resolved, std::move(first).value()));
        const auto original = Comet::ShaderProgramArtifact::load(
            ShaderProgramImport::artifact_path(paths, program), program);
        ASSERT_TRUE(original);
        ASSERT_TRUE(Comet::write_text_file_atomic(
            paths.assets() / "shaders/test.frag", "#version 450\ninvalid source\n"));
        ASSERT_TRUE(database.scan().succeeded());
        auto failed = ShaderProgramImport::prepare(paths, request());
        EXPECT_FALSE(failed);
        const auto retained = Comet::ShaderProgramArtifact::load(
            ShaderProgramImport::artifact_path(paths, program), program);
        ASSERT_TRUE(retained);
        EXPECT_EQ(retained->fragment_words, original->fragment_words);
    }

    TEST_F(ShaderProgramImportTest, RejectsStaleCandidateAfterIncludeChanges) {
        const auto resolved = request();
        auto prepared = ShaderProgramImport::prepare(paths, resolved);
        ASSERT_TRUE(prepared) << prepared.error().message;
        ASSERT_TRUE(Comet::write_text_file_atomic(
            paths.assets() / "shaders/color.glsl", "#define COLOR vec4(0.0,1.0,0.0,1.0)\n"));
        auto published =
            ShaderProgramImport::publish(database, paths, resolved, std::move(prepared).value());
        EXPECT_FALSE(published);
        EXPECT_FALSE(std::filesystem::exists(ShaderProgramImport::artifact_path(paths, program)));
    }

    TEST_F(ShaderProgramImportTest, RejectsStaleCandidateAfterDatabaseRevisionChanges) {
        const auto resolved = request();
        auto prepared = ShaderProgramImport::prepare(paths, resolved);
        ASSERT_TRUE(prepared) << prepared.error().message;
        ASSERT_TRUE(Comet::write_text_file_atomic(
            paths.assets() / "shaders/test.vert", std::string(VERTEX) + "\n"));
        ASSERT_TRUE(database.scan().succeeded());
        auto published =
            ShaderProgramImport::publish(database, paths, resolved, std::move(prepared).value());
        EXPECT_FALSE(published);
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
}

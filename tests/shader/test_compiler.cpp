#include "shader/compiler.h"
#include "graphics/pipeline/shader_interface.h"
#include "asset/handle.h"
#include "common/file_io.h"
#include "material_mesh_vert.h"
#include "material_textured_frag.h"
#include "material_solid_frag.h"
#include "debug_line_vert.h"
#include "debug_line_frag.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <future>

namespace Comet::Tests {
    class ShaderCompilerTest: public testing::Test {
    protected:
        std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / ("comet_shader_test_" + std::to_string(AssetHandle::generate().value()));
        ShaderCompiler::Request request{.source = root / "source.vert"};
        void TearDown() override {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
        void write(const std::string& path, const std::string& source) {
            write_text_file_atomic(root / path, source);
        }
    };

    TEST_F(ShaderCompilerTest, MatchesBuildTimeBytecodeForEveryProductionShader) {
        const auto compare = [](const char* filename, ShaderCompiler::Stage stage,
                                 std::span<const uint32_t> embedded) {
            ShaderCompiler::Request input{
                .source = std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/glsl"
                          / filename,
                .stage = stage};
            const auto result = ShaderCompiler::compile(input);
            ASSERT_TRUE(result.succeeded()) << result.diagnostics;
            EXPECT_TRUE(std::ranges::equal(result.words, embedded)) << filename;
            EXPECT_TRUE(ShaderCompiler::inputs_unchanged(result));
            EXPECT_EQ(result.dependencies.size(), 1u);
        };
        compare("material_mesh.vert", ShaderCompiler::Stage::Vertex, MATERIAL_MESH_VERT);
        compare("material_textured.frag", ShaderCompiler::Stage::Fragment,
            MATERIAL_TEXTURED_FRAG);
        compare(
            "material_solid.frag", ShaderCompiler::Stage::Fragment, MATERIAL_SOLID_FRAG);
        compare("debug_line.vert", ShaderCompiler::Stage::Vertex, DEBUG_LINE_VERT);
        compare("debug_line.frag", ShaderCompiler::Stage::Fragment, DEBUG_LINE_FRAG);
    }

    TEST_F(ShaderCompilerTest, HonorsStageEntryAndTarget) {
        write("source.vert", "#version 450\nvoid main(){gl_Position=vec4(1);}");
        request.entry_point = "vertex_entry";
        auto result = ShaderCompiler::compile(request);
        ASSERT_TRUE(result.succeeded()) << result.diagnostics;
        EXPECT_EQ(result.words[1], 0x10000u);
        EXPECT_EQ(ShaderInterface(result.words, "vertex_entry").get_stage(),
            vk::ShaderStageFlagBits::eVertex);
        EXPECT_THROW(ShaderInterface(result.words), std::invalid_argument);
        request.target = ShaderCompiler::Target::Vulkan13;
        result = ShaderCompiler::compile(request);
        ASSERT_TRUE(result.succeeded()) << result.diagnostics;
        EXPECT_EQ(result.words[1], 0x10600u);
        request.stage = ShaderCompiler::Stage::Fragment;
        EXPECT_FALSE(ShaderCompiler::compile(request).succeeded());
        write("source.vert", "#version 450\nlayout(local_size_x=1) in; void main(){}");
        request.stage = ShaderCompiler::Stage::Compute;
        result = ShaderCompiler::compile(request);
        ASSERT_TRUE(result.succeeded()) << result.diagnostics;
        EXPECT_EQ(ShaderInterface(result.words, "vertex_entry").get_stage(),
            vk::ShaderStageFlagBits::eCompute);
    }

    TEST_F(ShaderCompilerTest, ResolvesNestedIncludesAndDefinesWithOwnedSnapshot) {
        write("source.vert",
            "#version 450\n#extension GL_GOOGLE_include_directive : require\n#include \"sub/outer.glsl\"\nvoid main(){gl_Position=vec4(VALUE*SCALE);}");
        write("sub/outer.glsl", "#include \"value.glsl\"\n");
        write("sub/value.glsl", "#define VALUE 2.0\n");
        request.defines = {{"SCALE", "3.0"}};
        const auto result = ShaderCompiler::compile(request);
        ASSERT_TRUE(result.succeeded()) << result.diagnostics;
        ASSERT_EQ(result.dependencies.size(), 3u);
        EXPECT_TRUE(ShaderCompiler::inputs_unchanged(result));
        write("sub/value.glsl", "#define VALUE 4.0\n");
        EXPECT_FALSE(ShaderCompiler::inputs_unchanged(result));
        const auto changed = ShaderCompiler::compile(request);
        ASSERT_TRUE(changed.succeeded()) << changed.diagnostics;
        EXPECT_NE(result.words, changed.words);
        request.defines["SCALE"] = "2.0";
        const auto variant = ShaderCompiler::compile(request);
        ASSERT_TRUE(variant.succeeded());
        EXPECT_NE(changed.words, variant.words);
    }

    TEST_F(ShaderCompilerTest, TracksMissingSearchCandidatesAndLocalOverride) {
        write("source.vert",
            "#version 450\n#extension GL_GOOGLE_include_directive : require\n#include \"value.glsl\"\nvoid main(){gl_Position=vec4(VALUE);}");
        request.include_directories = {root / "shared"};
        auto result = ShaderCompiler::compile(request);
        EXPECT_FALSE(result.succeeded());
        EXPECT_NE(result.diagnostics.find("value.glsl"), std::string::npos);
        EXPECT_TRUE(ShaderCompiler::inputs_unchanged(result));
        write("shared/value.glsl", "#define VALUE 2.0\n");
        EXPECT_FALSE(ShaderCompiler::inputs_unchanged(result));
        result = ShaderCompiler::compile(request);
        ASSERT_TRUE(result.succeeded()) << result.diagnostics;
        ASSERT_EQ(result.dependencies.size(), 3u);
        EXPECT_EQ(std::ranges::count_if(result.dependencies,
                      [](const auto& input) { return !input.contents; }),
            1);
        write("value.glsl", "#define VALUE 3.0\n");
        EXPECT_FALSE(ShaderCompiler::inputs_unchanged(result));
        const auto local = ShaderCompiler::compile(request);
        ASSERT_TRUE(local.succeeded()) << local.diagnostics;
        EXPECT_NE(result.words, local.words);
    }

    TEST_F(ShaderCompilerTest, ReportsSourceErrorsAndRejectsInvalidOptions) {
        const auto missing = ShaderCompiler::compile(request);
        EXPECT_FALSE(missing.succeeded());
        EXPECT_FALSE(missing.diagnostics.empty());
        write("source.vert", "#version 450\nvoid main(){ invalid_token; }");
        const auto invalid = ShaderCompiler::compile(request);
        EXPECT_FALSE(invalid.succeeded());
        EXPECT_NE(invalid.diagnostics.find("source.vert"), std::string::npos);
        EXPECT_NE(invalid.diagnostics.find("2:"), std::string::npos);
        write("source.vert", "#version 450\nvoid main(){gl_Position=vec4(1);}");
        request.entry_point = "not an entry";
        EXPECT_FALSE(ShaderCompiler::compile(request).succeeded());
        request.entry_point = "main";
        request.defines = {{"VALUE", "1\n#error injection"}};
        EXPECT_FALSE(ShaderCompiler::compile(request).succeeded());
        request.defines = {{"not an identifier", "1"}};
        EXPECT_FALSE(ShaderCompiler::compile(request).succeeded());
        request.defines.clear();
        request.stage = static_cast<ShaderCompiler::Stage>(999);
        EXPECT_FALSE(ShaderCompiler::compile(request).succeeded());
        request.stage = ShaderCompiler::Stage::Vertex;
        request.target = static_cast<ShaderCompiler::Target>(999);
        EXPECT_FALSE(ShaderCompiler::compile(request).succeeded());
    }

    TEST_F(ShaderCompilerTest, BoundsRecursiveAndOversizedIncludes) {
        write("source.vert",
            "#version 450\n#extension GL_GOOGLE_include_directive : require\n#include \"loop.glsl\"\nvoid main(){gl_Position=vec4(1);}");
        write("loop.glsl", "#include \"loop.glsl\"\n");
        const auto recursive = ShaderCompiler::compile(request);
        EXPECT_FALSE(recursive.succeeded());
        EXPECT_NE(recursive.diagnostics.find("depth"), std::string::npos);
        write("loop.glsl", std::string(8 * 1024 * 1024 + 1, ' '));
        const auto oversized = ShaderCompiler::compile(request);
        EXPECT_FALSE(oversized.succeeded());
        EXPECT_NE(oversized.diagnostics.find("8 MiB"), std::string::npos);
    }

    TEST_F(ShaderCompilerTest, DetectsSymlinkRetargetEvenWhenOldFileStillExists) {
        write("first.vert", "#version 450\nvoid main(){gl_Position=vec4(1);}");
        write("second.vert", "#version 450\nvoid main(){gl_Position=vec4(2);}");
        std::error_code error;
        std::filesystem::create_symlink(root / "first.vert", request.source, error);
        if(error)
            GTEST_SKIP() << "Symlinks unavailable: " << error.message();
        const auto result = ShaderCompiler::compile(request);
        ASSERT_TRUE(result.succeeded()) << result.diagnostics;
        EXPECT_TRUE(ShaderCompiler::inputs_unchanged(result));
        std::filesystem::remove(request.source);
        std::filesystem::create_symlink(root / "second.vert", request.source);
        EXPECT_FALSE(ShaderCompiler::inputs_unchanged(result));
        EXPECT_TRUE(std::filesystem::exists(root / "first.vert"));
    }

    TEST_F(ShaderCompilerTest, ParallelRequestsDoNotShareDefinesOrResults) {
        write("source.vert", "#version 450\nvoid main(){gl_Position=vec4(VALUE);}");
        std::vector<std::future<ShaderCompiler::Result>> futures;
        for(int index = 0; index < 8; ++index) {
            auto input = request;
            input.defines = {{"VALUE", std::to_string(index) + ".0"}};
            futures.push_back(std::async(
                std::launch::async, [input] { return ShaderCompiler::compile(input); }));
        }
        std::vector<uint32_t> previous;
        for(auto& future : futures) {
            const auto result = future.get();
            ASSERT_TRUE(result.succeeded()) << result.diagnostics;
            EXPECT_NE(previous, result.words);
            EXPECT_TRUE(ShaderCompiler::inputs_unchanged(result));
            previous = result.words;
        }
    }
}

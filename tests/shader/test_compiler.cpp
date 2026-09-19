#include "shader/compiler.h"
#include "render/material/material_layout.h"
#include "graphics/pipeline/shader_interface.h"
#include "render/material/material.h"
#include "render/material/material_runtime.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"
#include "unlit_color_vert.h"
#include "directional_vert.h"
#include "directional_frag.h"
#include "unlit_texture_blend_vert.h"
#include "unlit_texture_blend_frag.h"
#include "unlit_color_frag.h"
#include "line_vert.h"
#include "line_frag.h"
#include "lambert_vert.h"
#include "lambert_frag.h"
#include "display_vert.h"
#include "display_frag.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <future>
#include <cstring>
#include <span>
#include <string_view>

namespace Comet::Tests {
    class ShaderCompilerTest: public testing::Test {
    protected:
        TemporaryDirectory directory;
        const std::filesystem::path root = directory.path();
        ShaderCompiler::Request request{.source = root / "source.vert"};
        void write(const std::string& path, const std::string& source) {
            const auto saved = write_text_file_atomic(root / path, source);
            ASSERT_TRUE(saved) << saved.error();
        }
        Result<ShaderInterface> reflect_source(const std::string& source) {
            write("source.vert", source);
            const auto compiled = ShaderCompiler::compile(request);
            if(!compiled.succeeded())
                return Result<ShaderInterface>::failure(compiled.diagnostics);
            return ShaderInterface::reflect(compiled.words);
        }
    };

    TEST_F(ShaderCompilerTest, FixedResourceContractDetectsMemberOrderAndMatrixStorageChanges) {
        const std::string frame =
            "layout(set=0,binding=0,std140) uniform Frame {mat4 view; mat4 projection;} frame;\n";
        const std::string object = "layout(push_constant) uniform Object {mat4 model;} object;\n";
        const std::string body =
            "void main(){gl_Position=frame.projection*frame.view*object.model*vec4(1);}";
        const auto original = reflect_source("#version 450\n" + frame + object + body);
        ASSERT_TRUE(original) << original.error();
        const auto changed_code = reflect_source(
            "#version 450\n" + frame + object
            + "void main(){gl_Position=2.0*frame.projection*frame.view*object.model*vec4(1);}");
        ASSERT_TRUE(changed_code) << changed_code.error();
        EXPECT_TRUE(original.value().has_same_resource_layout(changed_code.value()));
        for(const std::string replacement :
            {"layout(set=0,binding=0,std140) uniform Frame {mat4 projection; mat4 view;} frame;\n",
                "layout(set=0,binding=0,std140,row_major) uniform Frame {mat4 view; mat4 projection;} frame;\n"}) {
            const auto changed = reflect_source("#version 450\n" + replacement + object + body);
            ASSERT_TRUE(changed) << changed.error();
            EXPECT_EQ(original.value().get_bindings()[0].block_size,
                changed.value().get_bindings()[0].block_size);
            EXPECT_FALSE(original.value().has_same_resource_layout(changed.value()));
        }
        const auto changed_push = reflect_source(
            "#version 450\n" + frame
            + "layout(push_constant,row_major) uniform Object {mat4 model;} object;\n" + body);
        ASSERT_TRUE(changed_push) << changed_push.error();
        EXPECT_EQ(original.value().get_push_constants()[0].size,
            changed_push.value().get_push_constants()[0].size);
        EXPECT_FALSE(original.value().has_same_resource_layout(changed_push.value()));
    }

    TEST_F(ShaderCompilerTest, ResourceContractOwnsNestedMembersAndArrayShape) {
        const auto make = [&](std::string member) {
            return reflect_source(
                "#version 450\nstruct Item {" + member
                + "}; layout(set=0,binding=0,std140) uniform Data {Item items[2];} data;\n"
                  "void main(){gl_Position=vec4(data.items[0].value[0]);}");
        };
        const auto original = make("vec4 value[2];");
        ASSERT_TRUE(original) << original.error();
        const auto changed = make("ivec4 value[2];");
        ASSERT_TRUE(changed) << changed.error();
        const auto& items = original.value().get_bindings()[0].members[0];
        EXPECT_EQ(items.shape.array_dimensions, (std::vector<uint32_t>{2}));
        ASSERT_EQ(items.members.size(), 1u);
        EXPECT_EQ(items.members[0].shape.array_stride, 16u);
        EXPECT_EQ(items.members[0].shape.array_dimensions, (std::vector<uint32_t>{2}));
        EXPECT_EQ(items.members[0].shape.scalar, ShaderInterface::TypeShape::Scalar::Float);
        EXPECT_FALSE(original.value().has_same_resource_layout(changed.value()));
    }

    TEST_F(ShaderCompilerTest, MaterialTexturesRejectIncompatibleSampledImageShapes) {
        request.stage = ShaderStage::Fragment;
        const auto layout = MaterialLayout::create("sample", {{"image", 1, "Image"}});
        ASSERT_TRUE(layout) << layout.error();
        for(const auto& [type, expression] : {std::pair{"sampler2D", "texture(image,vec2(0))"},
                {"samplerCube", "texture(image,vec3(1))"},
                {"sampler2DArray", "texture(image,vec3(0))"},
                {"sampler2DShadow", "texture(image,vec3(0))"},
                {"isampler2D", "texture(image,vec2(0))"}, {"usampler2D", "texture(image,vec2(0))"},
                {"sampler2DMS", "texelFetch(image,ivec2(0),0)"}}) {
            SCOPED_TRACE(type);
            const auto shader = reflect_source(
                std::string("#version 450\nlayout(set=1,binding=1) uniform ") + type
                + " image; layout(location=0) out vec4 color; void main(){color=vec4(" + expression
                + ");}");
            ASSERT_TRUE(shader) << shader.error();
            EXPECT_EQ(bool(layout.value().validate(shader.value())),
                std::string_view(type) == "sampler2D");
        }
    }

    TEST_F(ShaderCompilerTest, RebindsRegisteredMaterialFieldsWithoutChangingSemanticMetadata) {
        request.stage = ShaderStage::Fragment;
        const auto original = MaterialLayout::find_builtin("unlit_color");
        const auto compiled = reflect_source(
            "#version 450\nlayout(set=1,binding=5,std140) uniform Params {"
            "float intensity; layout(offset=32) vec4 color;} material;\n"
            "layout(location=0) out vec4 result; void main(){result=material.color*material.intensity;}");
        ASSERT_TRUE(compiled) << compiled.error();
        const auto result = MaterialLayout::reflect(original, compiled.value());
        ASSERT_TRUE(result) << result.error();
        const auto layout = result.value();
        EXPECT_NE(layout, original);
        EXPECT_EQ(layout->get_parameter_binding(), 5u);
        EXPECT_EQ(layout->get_parameter_size(), 48u);
        EXPECT_EQ(layout->get_scalars()[0].offset, 0u);
        EXPECT_EQ(layout->get_vectors()[0].offset, 32u);
        EXPECT_EQ(layout->get_scalars()[0].default_value, original->get_scalars()[0].default_value);
        EXPECT_EQ(
            layout->get_vectors()[0].semantic, MaterialLayout::VectorProperty::Semantic::Color);
        EXPECT_EQ(layout->get_vectors()[0].display_name, original->get_vectors()[0].display_name);
        ASSERT_TRUE(MaterialLayout::reflect(layout, compiled.value()));
        EXPECT_EQ(MaterialLayout::reflect(layout, compiled.value()).value(), layout);
        EXPECT_EQ(original->get_parameter_size(), 32u);

        MaterialRuntimeCache cache;
        auto material = std::make_shared<Material>("solid", "unlit_color");
        EXPECT_TRUE(material->set_scalar_property("intensity", 0.25f));
        EXPECT_TRUE(material->set_vector_property("color", {0.2f, 0.4f, 0.6f, 1}));
        const auto previous = cache.prepare(AssetHandle(1), material, original);
        ASSERT_TRUE(previous);
        auto candidate = cache;
        const auto prepared = candidate.rebind(AssetHandle(1), layout);
        ASSERT_TRUE(prepared) << prepared.error();
        float intensity;
        Math::Vec4 color;
        std::memcpy(&intensity, prepared.value()->parameters.data(), sizeof(intensity));
        std::memcpy(&color, prepared.value()->parameters.data() + 32, sizeof(color));
        EXPECT_FLOAT_EQ(intensity, 0.25f);
        EXPECT_EQ(color, Math::Vec4(0.2f, 0.4f, 0.6f, 1));
        EXPECT_EQ(cache.prepare(AssetHandle(1), material, original).value(), previous.value());
        EXPECT_EQ(previous.value()->parameters.size(), 32u);
        EXPECT_FALSE(candidate.rebind(AssetHandle(99), layout));
        cache.swap(candidate);
        EXPECT_EQ(cache.prepare(AssetHandle(1), material, layout).value(), prepared.value());
    }

    TEST_F(ShaderCompilerTest, MaterialReflectionRejectsUnknownMissingAndChangedPropertyTypes) {
        request.stage = ShaderStage::Fragment;
        const auto metadata = MaterialLayout::find_builtin("unlit_color");
        for(const auto& [members, expression] :
            {std::pair{"vec4 renamed; float intensity;", "material.renamed*material.intensity"},
                {"vec4 color;", "material.color"},
                {"vec4 color; float intensity; float extra;",
                    "material.color*material.intensity*material.extra"},
                {"vec4 color; int intensity;", "material.color*float(material.intensity)"}}) {
            SCOPED_TRACE(members);
            const auto reflected = reflect_source(
                std::string("#version 450\nlayout(set=1,binding=3,std140) uniform Params {")
                + members + "} material; layout(location=0) out vec4 result; void main(){result="
                + expression + ";}");
            ASSERT_TRUE(reflected) << reflected.error();
            EXPECT_FALSE(MaterialLayout::reflect(metadata, reflected.value()));
        }
        EXPECT_FALSE(
            MaterialLayout::create("duplicate", {{"a", 1, "", "same"}, {"b", 2, "", "same"}}));
        EXPECT_FALSE(
            MaterialLayout::create("conflict", {{"image", 5}}, 16, {{"value", 0, 1}}, {}, 5));
    }

    TEST_F(ShaderCompilerTest, ReflectsTextureBindingByShaderNameNotPreviousBinding) {
        request.stage = ShaderStage::Fragment;
        const auto shader = reflect_source(
            "#version 450\nlayout(set=1,binding=9) uniform sampler2D image;"
            "layout(location=0) out vec4 result;void main(){result=texture(image,vec2(0));}");
        ASSERT_TRUE(shader) << shader.error();
        auto metadata =
            MaterialLayout::create("sample", {{"logical_texture", 1, "Texture", "image"}});
        ASSERT_TRUE(metadata) << metadata.error();
        const auto original = std::make_shared<MaterialLayout>(std::move(metadata).value());
        const auto result = MaterialLayout::reflect(original, shader.value());
        ASSERT_TRUE(result) << result.error();
        EXPECT_EQ(result.value()->get_textures()[0].binding, 9u);
        EXPECT_EQ(result.value()->get_textures()[0].name, "logical_texture");
        EXPECT_EQ(original->get_textures()[0].binding, 1u);
    }

    TEST_F(ShaderCompilerTest, RejectsUnsupportedUserStageInterfaces) {
        for(const auto& declaration :
            {"layout(location=0) in mat4 value; void main(){gl_Position=value[0];}",
                "layout(location=0) in vec4 value[2]; void main(){gl_Position=value[0];}",
                "layout(location=0) in dvec3 value; void main(){gl_Position=vec4(value,1);}",
                "layout(location=0,component=1) out vec2 value; void main(){value=vec2(1);gl_Position=vec4(0);}"}) {
            SCOPED_TRACE(declaration);
            write("source.vert", std::string("#version 450\n") + declaration);
            const auto result = ShaderCompiler::compile(request);
            ASSERT_TRUE(result.succeeded()) << result.diagnostics;
            const auto reflected = ShaderInterface::reflect(result.words);
            ASSERT_FALSE(reflected);
            EXPECT_NE(reflected.error().find("32-bit scalar/vector"), std::string::npos);
        }
    }

    TEST_F(ShaderCompilerTest, RejectsSpecializationDependentArrayReflection) {
        for(const std::string length : {"COUNT", "COUNT + 1"}) {
            write("source.vert",
                "#version 450\nlayout(constant_id=0) const int COUNT=2;\nlayout(set=0,binding=0) uniform sampler2D textures["
                    + length + "];\nvoid main(){gl_Position=texture(textures[0],vec2(0));}");
            const auto result = ShaderCompiler::compile(request);
            ASSERT_TRUE(result.succeeded()) << result.diagnostics;
            EXPECT_FALSE(ShaderInterface::reflect(result.words)) << length;
        }
    }

    TEST_F(ShaderCompilerTest, CompileTimeArrayVariantsReflectActualLayout) {
        write("source.vert",
            "#version 450\nlayout(set=0,binding=0) uniform sampler2D textures[COUNT+1];\nvoid main(){gl_Position=texture(textures[0],vec2(0));}");
        for(const uint32_t count : {2u, 7u}) {
            request.defines = {{"COUNT", std::to_string(count)}};
            const auto result = ShaderCompiler::compile(request);
            ASSERT_TRUE(result.succeeded()) << result.diagnostics;
            auto reflected_result = ShaderInterface::reflect(result.words);
            ASSERT_TRUE(reflected_result) << reflected_result.error();
            const auto reflected = std::move(reflected_result).value();
            ASSERT_EQ(reflected.get_bindings().size(), 1u);
            EXPECT_EQ(reflected.get_bindings()[0].count, count + 1);
            EXPECT_TRUE(reflected.get_specialization_constants().empty());
        }
    }

    TEST_F(ShaderCompilerTest, MatchesBuildTimeBytecodeForEveryProductionShader) {
        const auto compare = [](const char* filename, ShaderStage stage,
                                 std::span<const uint32_t> embedded) {
            ShaderCompiler::Request input{
                .source = std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders" / filename,
                .stage = stage};
            const auto result = ShaderCompiler::compile(input);
            ASSERT_TRUE(result.succeeded()) << result.diagnostics;
            EXPECT_TRUE(std::ranges::equal(result.words, embedded)) << filename;
            EXPECT_TRUE(ShaderCompiler::inputs_unchanged(result));
            EXPECT_FALSE(result.dependencies.empty());
        };
        compare("material/unlit_color.vert", ShaderStage::Vertex, UNLIT_COLOR_VERT);
        compare("material/unlit_texture_blend.vert", ShaderStage::Vertex, UNLIT_TEXTURE_BLEND_VERT);
        compare("material/lambert.vert", ShaderStage::Vertex, LAMBERT_VERT);
        compare(
            "material/unlit_texture_blend.frag", ShaderStage::Fragment, UNLIT_TEXTURE_BLEND_FRAG);
        compare("material/unlit_color.frag", ShaderStage::Fragment, UNLIT_COLOR_FRAG);
        compare("material/lambert.frag", ShaderStage::Fragment, LAMBERT_FRAG);
        compare("debug/line.vert", ShaderStage::Vertex, LINE_VERT);
        compare("debug/line.frag", ShaderStage::Fragment, LINE_FRAG);
        compare("post/display.vert", ShaderStage::Vertex, DISPLAY_VERT);
        compare("post/display.frag", ShaderStage::Fragment, DISPLAY_FRAG);
        compare("shadow/directional.vert", ShaderStage::Vertex, DIRECTIONAL_VERT);
        compare("shadow/directional.frag", ShaderStage::Fragment, DIRECTIONAL_FRAG);
    }

    TEST_F(ShaderCompilerTest, HonorsStageEntryAndTarget) {
        write("source.vert", "#version 450\nvoid main(){gl_Position=vec4(1);}");
        request.entry_point = "vertex_entry";
        auto result = ShaderCompiler::compile(request);
        ASSERT_TRUE(result.succeeded()) << result.diagnostics;
        EXPECT_EQ(result.words[1], 0x10000u);
        {
            auto candidate = ShaderInterface::reflect(result.words, "vertex_entry");
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_EQ(candidate.value().get_stage(), ShaderStage::Vertex);
        }
        EXPECT_FALSE(ShaderInterface::reflect(result.words));
        request.target = ShaderCompiler::Target::Vulkan13;
        result = ShaderCompiler::compile(request);
        ASSERT_TRUE(result.succeeded()) << result.diagnostics;
        EXPECT_EQ(result.words[1], 0x10600u);
        request.stage = ShaderStage::Fragment;
        EXPECT_FALSE(ShaderCompiler::compile(request).succeeded());
        write("source.vert", "#version 450\nlayout(local_size_x=1) in; void main(){}");
        request.stage = ShaderStage::Compute;
        result = ShaderCompiler::compile(request);
        ASSERT_TRUE(result.succeeded()) << result.diagnostics;
        {
            auto candidate = ShaderInterface::reflect(result.words, "vertex_entry");
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_EQ(candidate.value().get_stage(), ShaderStage::Compute);
        }
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
        EXPECT_EQ(std::ranges::count_if(
                      result.dependencies, [](const auto& input) { return !input.contents; }),
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
        request.stage = static_cast<ShaderStage>(999);
        EXPECT_FALSE(ShaderCompiler::compile(request).succeeded());
        request.stage = ShaderStage::Vertex;
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
        EXPECT_NE(oversized.diagnostics.find("source.vert"), std::string::npos);
        EXPECT_NE(oversized.diagnostics.find("3:"), std::string::npos);
        write("shared/loop.glsl", "// A fallback must not hide an unreadable local file.\n");
        request.include_directories = {root / "shared"};
        const auto fallback = ShaderCompiler::compile(request);
        EXPECT_FALSE(fallback.succeeded());
        EXPECT_NE(fallback.diagnostics.find("8 MiB"), std::string::npos);
    }

    TEST_F(ShaderCompilerTest, DistinguishesMissingInputsFromReadFailures) {
        const auto missing = ShaderCompiler::compile(request);
        ASSERT_FALSE(missing.succeeded());
        ASSERT_EQ(missing.dependencies.size(), 1u);
        EXPECT_FALSE(missing.dependencies.front().contents);
        EXPECT_TRUE(ShaderCompiler::inputs_unchanged(missing));

        write("source.vert", "#version 450\nvoid main(){gl_Position=vec4(1);}");
        EXPECT_FALSE(ShaderCompiler::inputs_unchanged(missing));
        const auto loaded = ShaderCompiler::compile(request);
        ASSERT_TRUE(loaded.succeeded()) << loaded.diagnostics;
        std::filesystem::remove(request.source);
        std::filesystem::create_directory(request.source);
        EXPECT_FALSE(ShaderCompiler::inputs_unchanged(loaded));
        const auto unreadable = ShaderCompiler::compile(request);
        EXPECT_FALSE(unreadable.succeeded());
        EXPECT_NE(unreadable.diagnostics.find("Cannot read shader source"), std::string::npos);
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
            futures.push_back(
                std::async(std::launch::async, [input] { return ShaderCompiler::compile(input); }));
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

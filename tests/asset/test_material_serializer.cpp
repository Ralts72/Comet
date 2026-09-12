#include "asset/serialization/material_serializer.h"
#include "asset/handle.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace Comet::Tests {
    TEST(MaterialSerializerTest,
        RoundTripsTypedParametersAndOnlyTracksTextureDependencies) {
        const MaterialData data{.template_name = "typed",
            .texture_properties = {{"albedo", AssetHandle(73)}},
            .scalar_properties = {{"intensity", 0.75f}},
            .vector_properties = {{"color", {0.2f, 0.4f, 0.6f, 1}}}};
        const MaterialSerializer serializer;
        const auto encoded = serializer.serialize(data);
        ASSERT_TRUE(encoded) << encoded.error();
        const auto decoded = serializer.deserialize(encoded.value());
        ASSERT_TRUE(decoded) << decoded.error();
        EXPECT_EQ(decoded.value(), data);
        EXPECT_EQ(get_asset_dependencies(decoded.value()), std::vector{AssetHandle(73)});
    }

    TEST(MaterialSerializerTest, RejectsInvalidTypedParametersAndCrossTypeNames) {
        const MaterialSerializer serializer;
        for(const auto property : {R"({"type":"scalar","value":"1"})",
                R"({"type":"scalar","value":true})", R"({"type":"scalar","value":1e100})",
                R"({"type":"scalar","value":1,"asset":73})",
                R"({"type":"texture","asset":73,"value":1})",
                R"({"type":"vector","value":[1,2,3]})",
                R"({"type":"vector","value":[1,2,3,4,5]})",
                R"({"type":"vector","value":[1,2,"3",4]})",
                R"({"type":"vector","value":[1,2,null,4]})"}) {
            SCOPED_TRACE(property);
            const auto result = serializer.deserialize(
                std::string(R"({"version":2,"template":"test","properties":{"value":)")
                    + property + "}}",
                "invalid.mat");
            ASSERT_FALSE(result);
            EXPECT_NE(result.error().find("invalid.mat"), std::string::npos);
            EXPECT_NE(result.error().find("properties.value"), std::string::npos);
        }
        EXPECT_FALSE(serializer.deserialize(
            R"({"version":2,"template":"test","properties":{"p":{"type":"scalar","value":1},"p":{"type":"vector","value":[1,1,1,1]}}})"));
        EXPECT_FALSE(serializer.serialize({.template_name = "test",
            .texture_properties = {{"p", AssetHandle(73)}},
            .scalar_properties = {{"p", 1}}}));
        EXPECT_FALSE(serializer.serialize({.template_name = "test",
            .scalar_properties = {{"p", 1}},
            .vector_properties = {{"p", {1, 1, 1, 1}}}}));
        EXPECT_FALSE(serializer.serialize(
            {.template_name = "test", .scalar_properties = {{"", 1}}}));
        EXPECT_FALSE(serializer.serialize({.template_name = "test",
            .scalar_properties = {{"p", std::numeric_limits<float>::infinity()}}}));
        EXPECT_FALSE(serializer.serialize({.template_name = "test",
            .vector_properties = {
                {"p", {1, 1, std::numeric_limits<float>::quiet_NaN(), 1}}}}));
    }

    namespace {
        class TemporaryMaterial final {
        public:
            explicit TemporaryMaterial(const std::string& contents) {
                m_path = std::filesystem::temp_directory_path()
                         / ("comet_material_serializer_test_"
                             + std::to_string(AssetHandle::generate().value()) + ".mat");
                std::ofstream output(m_path, std::ios::binary);
                output << contents;
            }

            ~TemporaryMaterial() {
                std::error_code error;
                std::filesystem::remove(m_path, error);
            }

            [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

        private:
            std::filesystem::path m_path;
        };
    }

    TEST(MaterialSerializerTest, LoadsTextureHandleProperties) {
        const TemporaryMaterial material(R"({
  "version": 2,
  "template": "unlit_texture_blend",
  "properties": {
    "u_Texture0": {"type": "texture", "asset": 42},
    "u_Texture1": {"type": "texture", "asset": 73}
  }
})");

        const MaterialData data = MaterialSerializer{}.load(material.path()).value();

        EXPECT_EQ(data.template_name, "unlit_texture_blend");
        ASSERT_EQ(data.texture_properties.size(), 2u);
        EXPECT_EQ(data.texture_properties.at("u_Texture0"), AssetHandle(42));
        EXPECT_EQ(data.texture_properties.at("u_Texture1"), AssetHandle(73));
    }

    TEST(MaterialSerializerTest, SerializesAndSavesDeterministically) {
        const MaterialData data{.template_name = "unlit_texture_blend",
            .texture_properties = {
                {"u_Texture0", AssetHandle(42)}, {"u_Texture1", AssetHandle(73)}}};
        const MaterialSerializer serializer;
        const std::string contents = serializer.serialize(data).value();

        EXPECT_EQ(contents, R"({
  "version": 2,
  "template": "unlit_texture_blend",
  "properties": {
    "u_Texture0": {
      "type": "texture",
      "asset": 42
    },
    "u_Texture1": {
      "type": "texture",
      "asset": 73
    }
  }
}
)");
        EXPECT_EQ(serializer.deserialize(contents).value(), data);

        const TemporaryMaterial material("");
        EXPECT_TRUE(serializer.save(data, material.path()));
        EXPECT_EQ(serializer.load(material.path()).value(), data);
    }

    TEST(MaterialSerializerTest, RejectsInvalidAssetReference) {
        const TemporaryMaterial material(R"({
  "version": 2,
  "template": "unlit_texture_blend",
  "properties": {"u_Texture0": {"type": "texture", "asset": 0}}
})");

        EXPECT_FALSE(MaterialSerializer{}.load(material.path()));
    }

    TEST(MaterialSerializerTest, RejectsUnsupportedPropertyType) {
        const TemporaryMaterial material(R"({
  "version": 2,
  "template": "unlit_texture_blend",
  "properties": {"roughness": {"type": "float", "asset": 42}}
})");

        EXPECT_FALSE(MaterialSerializer{}.load(material.path()));
    }

    TEST(MaterialSerializerTest, RejectsUnknownFields) {
        const TemporaryMaterial material(
            R"({"version": 2, "template": "unlit_texture_blend", "properties": {}, "extra": true})");

        EXPECT_FALSE(MaterialSerializer{}.load(material.path()));
    }

    TEST(MaterialSerializerTest, RejectsInvalidDataBeforeSaving) {
        const MaterialSerializer serializer;
        const TemporaryMaterial material(
            R"({"version": 2, "template": "unlit_texture_blend", "properties": {}})");
        const MaterialData original = serializer.load(material.path()).value();

        EXPECT_FALSE(
            serializer.serialize({.template_name = "", .texture_properties = {}}));
        EXPECT_FALSE(serializer.serialize({.template_name = "unlit_texture_blend",
            .texture_properties = {{"u_Texture0", INVALID_ASSET_HANDLE}}}));
        EXPECT_FALSE(serializer.save(
            {.template_name = "", .texture_properties = {}}, material.path()));
        EXPECT_EQ(serializer.load(material.path()).value(), original);
    }

    TEST(MaterialSerializerTest, ReportsJsonAndIoFailuresWithoutThrowing) {
        const MaterialSerializer serializer;
        const auto syntax = serializer.deserialize(R"({"version": [)", "broken.mat");
        ASSERT_FALSE(syntax);
        EXPECT_NE(syntax.error().find("broken.mat"), std::string::npos);

        const TemporaryMaterial parent("existing file");
        const auto blocked = parent.path() / "child.mat";
        const auto missing = serializer.load(blocked);
        ASSERT_FALSE(missing);
        EXPECT_NE(missing.error().find("child.mat"), std::string::npos);
        const auto write = serializer.save({.template_name = "test"}, blocked);
        ASSERT_FALSE(write);
        EXPECT_NE(write.error().find(parent.path().string()), std::string::npos);
        EXPECT_TRUE(std::filesystem::is_regular_file(parent.path()));
    }

    TEST(MaterialSerializerTest, PreservesStringsAndFullWidthHandlesInJson) {
        const MaterialData data{.template_name = "材质\"\\\n\t",
            .texture_properties = {{std::string("slot\0name", 9),
                AssetHandle(std::numeric_limits<std::uint64_t>::max())}}};
        const MaterialSerializer serializer;
        const auto contents = serializer.serialize(data);
        ASSERT_TRUE(contents) << contents.error();
        EXPECT_NE(contents.value().find("18446744073709551615"), std::string::npos);
        const auto loaded = serializer.deserialize(contents.value());
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value(), data);
        EXPECT_EQ(serializer.serialize(loaded.value()).value(), contents.value());

        const TemporaryMaterial material(contents.value());
        EXPECT_FALSE(
            serializer.save({.template_name = std::string(1, '\xff')}, material.path()));
        EXPECT_EQ(serializer.load(material.path()).value(), data);
    }

    TEST(MaterialSerializerTest, RequiresStrictJsonAndTypedNumbers) {
        const MaterialSerializer serializer;
        EXPECT_FALSE(
            serializer.deserialize("version: 1\ntemplate: test\nproperties: {}\n"));
        EXPECT_FALSE(serializer.deserialize(
            R"({"version": "2", "template": "test", "properties": {}})"));
        EXPECT_FALSE(serializer.deserialize(
            R"({"version": 2, "template": "test", "properties": {},})"));
        EXPECT_FALSE(serializer.deserialize(
            R"({"version": 2, /* comment */ "template": "test", "properties": {}})"));
        for(const auto asset : {"\"42\"", "42.5", "-1", "18446744073709551616"}) {
            SCOPED_TRACE(asset);
            EXPECT_FALSE(serializer.deserialize(
                std::string(
                    R"({"version": 2, "template": "test", "properties": {"slot": {"type": "texture", "asset": )")
                + asset + "}}}"));
        }
    }

    TEST(MaterialSerializerTest, PreservesSourceAndFieldDiagnostics) {
        const MaterialSerializer serializer;
        const auto missing = serializer.deserialize(R"({"version": 2})", "missing.mat");
        ASSERT_FALSE(missing);
        EXPECT_EQ(missing.error(),
            "Invalid material 'missing.mat' at '<root>': missing required field 'template'");

        const auto duplicate = serializer.deserialize(
            R"({"version": 2, "version": 2, "template": "test", "properties": {}})",
            "duplicate.mat");
        ASSERT_FALSE(duplicate);
        EXPECT_EQ(duplicate.error(),
            "Invalid material 'duplicate.mat' at '<root>': duplicate field 'version'");

        const auto scalar =
            serializer.deserialize(R"({"version": "nope"})", "scalar.mat");
        ASSERT_FALSE(scalar);
        EXPECT_EQ(scalar.error(),
            "Invalid material 'scalar.mat' at 'version': expected an unsigned integer");
    }
}

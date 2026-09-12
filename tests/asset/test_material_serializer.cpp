#include "asset/serialization/material_serializer.h"
#include "asset/handle.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace Comet::Tests {
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

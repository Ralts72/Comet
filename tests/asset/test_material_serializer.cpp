#include "asset/serialization/material_serializer.h"
#include "asset/handle.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
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
        const TemporaryMaterial material(R"(
version: 1
template: unlit_texture_blend
properties:
  u_Texture0:
    type: texture
    asset: 42
  u_Texture1:
    type: texture
    asset: 73
)");

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

        EXPECT_EQ(contents, "version: 1\ntemplate: unlit_texture_blend\nproperties:\n"
                            "  u_Texture0:\n    type: texture\n    asset: 42\n"
                            "  u_Texture1:\n    type: texture\n    asset: 73\n");
        EXPECT_EQ(serializer.deserialize(contents).value(), data);

        const TemporaryMaterial material("");
        EXPECT_TRUE(serializer.save(data, material.path()));
        EXPECT_EQ(serializer.load(material.path()).value(), data);
    }

    TEST(MaterialSerializerTest, RejectsInvalidAssetReference) {
        const TemporaryMaterial material(R"(
version: 1
template: unlit_texture_blend
properties:
  u_Texture0:
    type: texture
    asset: 0
)");

        EXPECT_FALSE(MaterialSerializer{}.load(material.path()));
    }

    TEST(MaterialSerializerTest, RejectsUnsupportedPropertyType) {
        const TemporaryMaterial material(R"(
version: 1
template: unlit_texture_blend
properties:
  roughness:
    type: float
    asset: 42
)");

        EXPECT_FALSE(MaterialSerializer{}.load(material.path()));
    }

    TEST(MaterialSerializerTest, RejectsUnknownFields) {
        const TemporaryMaterial material(R"(
version: 1
template: unlit_texture_blend
properties: {}
extra: true
)");

        EXPECT_FALSE(MaterialSerializer{}.load(material.path()));
    }

    TEST(MaterialSerializerTest, RejectsInvalidDataBeforeSaving) {
        const MaterialSerializer serializer;
        const TemporaryMaterial material(
            "version: 1\ntemplate: unlit_texture_blend\nproperties: {}\n");
        const MaterialData original = serializer.load(material.path()).value();

        EXPECT_FALSE(
            serializer.serialize({.template_name = "", .texture_properties = {}}));
        EXPECT_FALSE(serializer.serialize({.template_name = "unlit_texture_blend",
            .texture_properties = {{"u_Texture0", INVALID_ASSET_HANDLE}}}));
        EXPECT_FALSE(serializer.save(
            {.template_name = "", .texture_properties = {}}, material.path()));
        EXPECT_EQ(serializer.load(material.path()).value(), original);
    }

    TEST(MaterialSerializerTest, ReportsYamlAndIoFailuresWithoutThrowing) {
        const MaterialSerializer serializer;
        const auto syntax = serializer.deserialize("version: [", "broken.mat");
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

    TEST(MaterialSerializerTest, PreservesSourceAndFieldDiagnostics) {
        const MaterialSerializer serializer;
        const auto missing = serializer.deserialize("version: 1\n", "missing.mat");
        ASSERT_FALSE(missing);
        EXPECT_EQ(missing.error(),
            "Invalid material 'missing.mat' at '<root>': missing required field 'template'");

        const auto duplicate = serializer.deserialize(
            "version: 1\nversion: 1\ntemplate: test\nproperties: {}\n", "duplicate.mat");
        ASSERT_FALSE(duplicate);
        EXPECT_EQ(duplicate.error(),
            "Invalid material 'duplicate.mat' at '<root>': duplicate field 'version'");

        const auto scalar = serializer.deserialize("version: nope\n", "scalar.mat");
        ASSERT_FALSE(scalar);
        EXPECT_EQ(scalar.error(),
            "Invalid material 'scalar.mat' at 'version': expected an unsigned integer");
    }
}

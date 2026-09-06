#include "asset/serialization/material_serializer.h"
#include "asset/handle.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <limits>

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
template: cube_texture
properties:
  u_Texture0:
    type: texture
    asset: 42
  u_Texture1:
    type: texture
    asset: 73
)");

        const MaterialData data = MaterialSerializer{}.load(material.path());

        EXPECT_EQ(data.template_name, "cube_texture");
        ASSERT_EQ(data.texture_properties.size(), 2u);
        EXPECT_EQ(data.texture_properties.at("u_Texture0"), AssetHandle(42));
        EXPECT_EQ(data.texture_properties.at("u_Texture1"), AssetHandle(73));
    }

    TEST(MaterialSerializerTest,
        RoundTripsTypedParametersAndOnlyCollectsTextureDependencies) {
        const MaterialData data{.template_name = "cube_texture",
            .texture_properties = {{"u_Texture0", AssetHandle(42)}},
            .scalar_properties = {{"blend", 0.25f}},
            .vector_properties = {{"tint", {1, 0.5f, 0.25f, 1}}}};
        const MaterialSerializer serializer;
        const auto text = serializer.serialize(data);
        EXPECT_EQ(serializer.deserialize(text), data);
        EXPECT_EQ(get_asset_dependencies(data), std::vector{AssetHandle(42)});
    }

    TEST(MaterialSerializerTest, RejectsInvalidParametersBeforePublication) {
        const MaterialSerializer serializer;
        EXPECT_THROW(static_cast<void>(serializer.deserialize(
                         "version: 1\ntemplate: unlit_color\nproperties:\n"
                         "  color: {type: vector, value: [1, 2, 3]}\n")),
            std::runtime_error);
        EXPECT_THROW(static_cast<void>(serializer.deserialize(
                         "version: 1\ntemplate: unlit_color\nproperties:\n"
                         "  intensity: {type: scalar, value: .nan}\n")),
            std::runtime_error);
        EXPECT_THROW(
            static_cast<void>(serializer.deserialize(
                "version: 1\ntemplate: unlit_color\nproperties:\n"
                "  x: {type: scalar, value: 1}\n  x: {type: vector, value: [1, 1, 1, 1]}\n")),
            std::runtime_error);
        EXPECT_THROW(static_cast<void>(serializer.serialize({.template_name = "test",
                         .texture_properties = {{"same", AssetHandle(1)}},
                         .scalar_properties = {{"same", 1}}})),
            std::runtime_error);
        EXPECT_THROW(static_cast<void>(serializer.serialize({.template_name = "test",
                         .vector_properties = {{"color",
                             {1, 1, std::numeric_limits<float>::infinity(), 1}}}})),
            std::runtime_error);
    }

    TEST(MaterialSerializerTest, SerializesAndSavesDeterministically) {
        const MaterialData data{.template_name = "cube_texture",
            .texture_properties = {
                {"u_Texture0", AssetHandle(42)}, {"u_Texture1", AssetHandle(73)}}};
        const MaterialSerializer serializer;
        const std::string contents = serializer.serialize(data);

        EXPECT_EQ(contents, "version: 1\ntemplate: cube_texture\nproperties:\n"
                            "  u_Texture0:\n    type: texture\n    asset: 42\n"
                            "  u_Texture1:\n    type: texture\n    asset: 73\n");
        EXPECT_EQ(serializer.deserialize(contents), data);

        const TemporaryMaterial material("");
        serializer.save(data, material.path());
        EXPECT_EQ(serializer.load(material.path()), data);
    }

    TEST(MaterialSerializerTest, RejectsInvalidAssetReference) {
        const TemporaryMaterial material(R"(
version: 1
template: cube_texture
properties:
  u_Texture0:
    type: texture
    asset: 0
)");

        EXPECT_THROW(static_cast<void>(MaterialSerializer{}.load(material.path())),
            std::runtime_error);
    }

    TEST(MaterialSerializerTest, RejectsUnsupportedPropertyType) {
        const TemporaryMaterial material(R"(
version: 1
template: cube_texture
properties:
  roughness:
    type: float
    asset: 42
)");

        EXPECT_THROW(static_cast<void>(MaterialSerializer{}.load(material.path())),
            std::runtime_error);
    }

    TEST(MaterialSerializerTest, RejectsUnknownFields) {
        const TemporaryMaterial material(R"(
version: 1
template: cube_texture
properties: {}
extra: true
)");

        EXPECT_THROW(static_cast<void>(MaterialSerializer{}.load(material.path())),
            std::runtime_error);
    }

    TEST(MaterialSerializerTest, RejectsInvalidDataBeforeSaving) {
        const MaterialSerializer serializer;
        const TemporaryMaterial material(
            "version: 1\ntemplate: cube_texture\nproperties: {}\n");
        const MaterialData original = serializer.load(material.path());

        EXPECT_THROW(static_cast<void>(serializer.serialize(
                         {.template_name = "", .texture_properties = {}})),
            std::runtime_error);
        EXPECT_THROW(
            static_cast<void>(serializer.serialize({.template_name = "cube_texture",
                .texture_properties = {{"u_Texture0", INVALID_ASSET_HANDLE}}})),
            std::runtime_error);
        EXPECT_THROW(serializer.save({.template_name = "", .texture_properties = {}},
                         material.path()),
            std::runtime_error);
        EXPECT_EQ(serializer.load(material.path()), original);
    }
}

#include "asset/serialization/material_serializer.h"
#include "asset/handle.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace Comet::Tests {
    namespace {
        class TemporaryMaterial final {
        public:
            explicit TemporaryMaterial(const std::string& contents) {
                m_path = std::filesystem::temp_directory_path()
                    / ("comet_material_serializer_test_"
                       + std::to_string(AssetHandle::generate().value())
                       + ".mat");
                std::ofstream output(m_path, std::ios::binary);
                output << contents;
            }

            ~TemporaryMaterial() {
                std::error_code error;
                std::filesystem::remove(m_path, error);
            }

            [[nodiscard]] const std::filesystem::path& path() const {
                return m_path;
            }

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

    TEST(MaterialSerializerTest, SerializesAndSavesDeterministically) {
        const MaterialData data{
            .template_name = "cube_texture",
            .texture_properties = {
                {"u_Texture0", AssetHandle(42)},
                {"u_Texture1", AssetHandle(73)}
            }
        };
        const MaterialSerializer serializer;
        const std::string contents = serializer.serialize(data);

        EXPECT_EQ(
            contents,
            "version: 1\ntemplate: cube_texture\nproperties:\n"
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

        EXPECT_THROW(
            static_cast<void>(MaterialSerializer{}.load(material.path())),
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

        EXPECT_THROW(
            static_cast<void>(MaterialSerializer{}.load(material.path())),
            std::runtime_error);
    }

    TEST(MaterialSerializerTest, RejectsUnknownFields) {
        const TemporaryMaterial material(R"(
version: 1
template: cube_texture
properties: {}
extra: true
)");

        EXPECT_THROW(
            static_cast<void>(MaterialSerializer{}.load(material.path())),
            std::runtime_error);
    }

    TEST(MaterialSerializerTest, RejectsInvalidDataBeforeSaving) {
        const MaterialSerializer serializer;

        EXPECT_THROW(
            static_cast<void>(serializer.serialize({
                .template_name = "",
                .texture_properties = {}
            })),
            std::runtime_error);
        EXPECT_THROW(
            static_cast<void>(serializer.serialize({
                .template_name = "cube_texture",
                .texture_properties = {
                    {"u_Texture0", INVALID_ASSET_HANDLE}
                }
            })),
            std::runtime_error);
    }
}

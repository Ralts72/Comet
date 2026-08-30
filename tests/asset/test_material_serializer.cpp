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
}

#include "asset/metadata.h"
#include "asset/serialization/metadata_serializer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

namespace Comet::Tests {
    namespace {
        class TemporaryDirectory final {
        public:
            TemporaryDirectory() {
                m_path = std::filesystem::temp_directory_path()
                    / ("comet_asset_metadata_test_"
                       + std::to_string(AssetHandle::generate().value()));
                std::filesystem::create_directories(m_path);
            }

            ~TemporaryDirectory() {
                std::error_code error;
                std::filesystem::remove_all(m_path, error);
            }

            [[nodiscard]] const std::filesystem::path& path() const {
                return m_path;
            }

        private:
            std::filesystem::path m_path;
        };
    }

    TEST(AssetMetadataTest, SerializesStableGuidAndType) {
        const AssetMetadata metadata{
            .handle = AssetHandle(42),
            .type = AssetType::Texture,
            .import_settings = TextureImportSettings{
                .color_space = TextureColorSpace::Linear,
                .flip_y = true
            }
        };
        const AssetMetadataSerializer serializer;

        const std::string contents = serializer.serialize(metadata);

        EXPECT_EQ(
            contents,
            "version: 2\nguid: 42\ntype: texture\nimporter:\n"
            "  color_space: linear\n  flip_y: true\n");
        EXPECT_EQ(serializer.deserialize(contents), metadata);
    }

    TEST(AssetMetadataTest, SavesAndLoadsSidecarFile) {
        const TemporaryDirectory directory;
        const std::filesystem::path asset_path = directory.path() / "albedo.png";
        const std::filesystem::path sidecar_path = metadata_path(asset_path);
        const AssetMetadata metadata{
            .handle = AssetHandle(73),
            .type = AssetType::Material
        };
        const AssetMetadataSerializer serializer;

        serializer.save(metadata, sidecar_path);

        EXPECT_EQ(sidecar_path, directory.path() / "albedo.png.meta");
        EXPECT_EQ(serializer.load(sidecar_path), metadata);
    }

    TEST(AssetMetadataTest, PreservesFullGuidRange) {
        const AssetMetadata metadata{
            .handle = AssetHandle(std::numeric_limits<std::uint64_t>::max()),
            .type = AssetType::Mesh
        };
        const AssetMetadataSerializer serializer;

        EXPECT_EQ(serializer.deserialize(serializer.serialize(metadata)), metadata);
    }

    TEST(AssetMetadataTest, SupportsDeclaredAssetTypes) {
        constexpr AssetType types[] = {
            AssetType::Texture,
            AssetType::Material,
            AssetType::Mesh,
            AssetType::Shader,
            AssetType::Scene
        };

        for(const AssetType type: types) {
            SCOPED_TRACE(std::string(to_string(type)));
            EXPECT_EQ(asset_type_from_string(to_string(type)), type);
        }
        EXPECT_FALSE(asset_type_from_string("unknown"));
        EXPECT_FALSE(asset_type_from_string("Texture"));
    }

    TEST(AssetMetadataTest, RejectsInvalidIdentityAndType) {
        const AssetMetadataSerializer serializer;

        EXPECT_THROW(
            static_cast<void>(serializer.deserialize(
                "version: 2\nguid: 0\ntype: material\n")),
            std::runtime_error);
        EXPECT_THROW(
            static_cast<void>(serializer.deserialize(
                "version: 2\nguid: 42\ntype: audio\n")),
            std::runtime_error);
        EXPECT_THROW(
            static_cast<void>(serializer.serialize({
                .handle = INVALID_ASSET_HANDLE,
                .type = AssetType::Texture,
                .import_settings = TextureImportSettings{}
            })),
            std::runtime_error);
    }

    TEST(AssetMetadataTest, RejectsMalformedContract) {
        const AssetMetadataSerializer serializer;

        EXPECT_THROW(
            static_cast<void>(serializer.deserialize(
                "version: 3\nguid: 42\ntype: material\n")),
            std::runtime_error);
        EXPECT_THROW(
            static_cast<void>(serializer.deserialize(
                "version: 2\ntype: material\n")),
            std::runtime_error);
        EXPECT_THROW(
            static_cast<void>(serializer.deserialize(
                "version: 2\nguid: 42\ntype: material\nextra: true\n")),
            std::runtime_error);
    }

    TEST(AssetMetadataTest, ValidatesTextureImportSettings) {
        const AssetMetadataSerializer serializer;

        EXPECT_THROW(
            static_cast<void>(serializer.deserialize(
                "version: 2\nguid: 42\ntype: texture\n")),
            std::runtime_error);
        EXPECT_THROW(
            static_cast<void>(serializer.deserialize(
                "version: 2\nguid: 42\ntype: texture\n"
                "importer:\n  color_space: display_p3\n"
                "  flip_y: false\n")),
            std::runtime_error);
        EXPECT_THROW(
            static_cast<void>(serializer.deserialize(
                "version: 2\nguid: 42\ntype: texture\n"
                "importer:\n  color_space: srgb\n"
                "  flip_y: false\n  compression: high\n")),
            std::runtime_error);
        EXPECT_THROW(
            static_cast<void>(serializer.deserialize(
                "version: 2\nguid: 42\ntype: material\n"
                "importer:\n  color_space: srgb\n"
                "  flip_y: false\n")),
            std::runtime_error);
        EXPECT_THROW(
            static_cast<void>(serializer.serialize({
                .handle = AssetHandle(42),
                .type = AssetType::Texture
            })),
            std::runtime_error);

        EXPECT_EQ(
            texture_color_space_from_string("srgb"),
            TextureColorSpace::Srgb);
        EXPECT_EQ(
            texture_color_space_from_string("linear"),
            TextureColorSpace::Linear);
        EXPECT_FALSE(texture_color_space_from_string("SRGB"));
    }
}

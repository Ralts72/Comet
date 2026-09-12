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

            [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

        private:
            std::filesystem::path m_path;
        };
    }

    TEST(AssetMetadataTest, SerializesStableGuidAndType) {
        const AssetMetadata metadata{.handle = AssetHandle(42),
            .type = AssetType::Texture,
            .import_settings = TextureImportSettings{
                .color_space = TextureColorSpace::Linear, .flip_y = true}};
        const MetadataSerializer serializer;

        const std::string contents = serializer.serialize(metadata).value();

        EXPECT_EQ(contents, "version: 2\nguid: 42\ntype: texture\nimporter:\n"
                            "  color_space: linear\n  flip_y: true\n");
        EXPECT_EQ(serializer.deserialize(contents).value(), metadata);
    }

    TEST(AssetMetadataTest, SavesAndLoadsSidecarFile) {
        const TemporaryDirectory directory;
        const std::filesystem::path asset_path = directory.path() / "albedo.png";
        const std::filesystem::path sidecar_path = metadata_path(asset_path);
        const AssetMetadata metadata{
            .handle = AssetHandle(73), .type = AssetType::Material};
        const MetadataSerializer serializer;

        EXPECT_TRUE(serializer.save(metadata, sidecar_path));

        EXPECT_EQ(sidecar_path, directory.path() / "albedo.png.meta");
        EXPECT_EQ(serializer.load(sidecar_path).value(), metadata);
    }

    TEST(AssetMetadataTest, PreservesFullGuidRange) {
        const AssetMetadata metadata{
            .handle = AssetHandle(std::numeric_limits<std::uint64_t>::max()),
            .type = AssetType::Mesh};
        const MetadataSerializer serializer;

        EXPECT_EQ(serializer.deserialize(serializer.serialize(metadata).value()).value(),
            metadata);
    }

    TEST(AssetMetadataTest, SupportsDeclaredAssetTypes) {
        constexpr AssetType types[] = {AssetType::Texture, AssetType::Material,
            AssetType::Mesh, AssetType::Shader, AssetType::Scene};

        for(const AssetType type : types) {
            SCOPED_TRACE(std::string(to_string(type)));
            EXPECT_EQ(asset_type_from_string(to_string(type)), type);
        }
        EXPECT_FALSE(asset_type_from_string("unknown"));
        EXPECT_FALSE(asset_type_from_string("Texture"));
    }

    TEST(AssetMetadataTest, RejectsInvalidIdentityAndType) {
        const MetadataSerializer serializer;

        EXPECT_FALSE(serializer.deserialize("version: 2\nguid: 0\ntype: material\n"));
        EXPECT_FALSE(serializer.deserialize("version: 2\nguid: 42\ntype: audio\n"));
        EXPECT_FALSE(serializer.serialize({.handle = INVALID_ASSET_HANDLE,
            .type = AssetType::Texture,
            .import_settings = TextureImportSettings{}}));
    }

    TEST(AssetMetadataTest, RejectsMalformedContract) {
        const MetadataSerializer serializer;

        EXPECT_FALSE(serializer.deserialize("version: 3\nguid: 42\ntype: material\n"));
        EXPECT_FALSE(serializer.deserialize("version: 2\ntype: material\n"));
        EXPECT_FALSE(serializer.deserialize(
            "version: 2\nguid: 42\ntype: material\nextra: true\n"));
    }

    TEST(AssetMetadataTest, ValidatesTextureImportSettings) {
        const MetadataSerializer serializer;

        EXPECT_FALSE(serializer.deserialize("version: 2\nguid: 42\ntype: texture\n"));
        EXPECT_FALSE(serializer.deserialize("version: 2\nguid: 42\ntype: texture\n"
                                            "importer:\n  color_space: display_p3\n"
                                            "  flip_y: false\n"));
        EXPECT_FALSE(serializer.deserialize("version: 2\nguid: 42\ntype: texture\n"
                                            "importer:\n  color_space: srgb\n"
                                            "  flip_y: false\n  compression: high\n"));
        EXPECT_FALSE(serializer.deserialize("version: 2\nguid: 42\ntype: material\n"
                                            "importer:\n  color_space: srgb\n"
                                            "  flip_y: false\n"));
        EXPECT_FALSE(serializer.serialize(
            {.handle = AssetHandle(42), .type = AssetType::Texture}));

        EXPECT_EQ(texture_color_space_from_string("srgb"), TextureColorSpace::Srgb);
        EXPECT_EQ(texture_color_space_from_string("linear"), TextureColorSpace::Linear);
        EXPECT_FALSE(texture_color_space_from_string("SRGB"));
    }

    TEST(AssetMetadataTest, PreservesSourceAndNestedFieldDiagnostics) {
        const MetadataSerializer serializer;
        const auto missing = serializer.deserialize("version: 2\n", "missing.meta");
        ASSERT_FALSE(missing);
        EXPECT_EQ(missing.error(),
            "Invalid asset metadata 'missing.meta' at '<root>': missing required field 'guid'");

        const auto duplicate = serializer.deserialize(
            "version: 2\nguid: 42\nguid: 73\ntype: mesh\n", "duplicate.meta");
        ASSERT_FALSE(duplicate);
        EXPECT_EQ(duplicate.error(),
            "Invalid asset metadata 'duplicate.meta' at '<root>': duplicate field 'guid'");

        const auto scalar =
            serializer.deserialize("version: 2\nguid: 42\ntype: texture\nimporter:\n"
                                   "  color_space: srgb\n  flip_y: not-a-bool\n",
                "scalar.meta");
        ASSERT_FALSE(scalar);
        EXPECT_EQ(scalar.error(),
            "Invalid asset metadata 'scalar.meta' at 'importer.flip_y': expected a boolean");

        const auto yaml = serializer.deserialize("version: [", "syntax.meta");
        ASSERT_FALSE(yaml);
        EXPECT_TRUE(yaml.error().starts_with(
            "Invalid asset metadata 'syntax.meta' at '<yaml>':"));
    }
}

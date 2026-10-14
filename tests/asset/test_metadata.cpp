#include "asset/metadata.h"
#include "asset/serialization/metadata_serializer.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

namespace Comet::Tests {
    TEST(AssetMetadataTest, ModelSubresourcesRoundTripAndRejectAmbiguousIdentity) {
        AssetMetadata metadata{.handle = AssetHandle{1},
            .type = AssetType::Model,
            .subresources = {{"mesh/name:Body/primitive:0", AssetHandle{2}, AssetType::Mesh},
                {"material/name:Paint", AssetHandle{3}, AssetType::Material},
                {"texture/name:Paint/srgb", AssetHandle{4}, AssetType::Texture}}};
        const MetadataSerializer serializer;
        const auto text = serializer.serialize(metadata);
        ASSERT_TRUE(text) << text.error();
        EXPECT_EQ(serializer.deserialize(text.value()).value(), metadata);
        for(const auto& invalid : {AssetSubresource{"other", AssetHandle{1}, AssetType::Mesh},
                AssetSubresource{"other", AssetHandle{2}, AssetType::Mesh},
                AssetSubresource{metadata.subresources[0].key, AssetHandle{9}, AssetType::Mesh},
                AssetSubresource{"other", AssetHandle{9}, AssetType::Model}}) {
            auto candidate = metadata;
            candidate.subresources.push_back(invalid);
            EXPECT_FALSE(serializer.serialize(candidate));
        }
        metadata.type = AssetType::Mesh;
        EXPECT_FALSE(serializer.serialize(metadata));
        EXPECT_FALSE(serializer.deserialize(R"({"version":3,"guid":1,"type":"model",
            "subresources":[{"key":"a","guid":2,"type":"mesh"},{"key":"b","guid":2,"type":"texture"}]})"));
    }

    TEST(AssetMetadataTest, SerializesStableGuidAndType) {
        const AssetMetadata metadata{.handle = AssetHandle(42),
            .type = AssetType::Texture,
            .import_settings =
                TextureImportSettings{.color_space = TextureColorSpace::Linear, .flip_y = true}};
        const MetadataSerializer serializer;

        const std::string contents = serializer.serialize(metadata).value();

        EXPECT_EQ(contents, R"({
  "version": 3,
  "guid": 42,
  "type": "texture",
  "importer": {
    "color_space": "linear",
    "flip_y": true,
    "generate_mipmaps": true,
    "normal_map": false
  }
}
)");
        EXPECT_EQ(serializer.deserialize(contents).value(), metadata);
    }

    TEST(AssetMetadataTest, SavesAndLoadsSidecarFile) {
        const TemporaryDirectory directory;
        const std::filesystem::path asset_path = directory.path() / "albedo.png";
        const std::filesystem::path sidecar_path = metadata_path(asset_path);
        const AssetMetadata metadata{.handle = AssetHandle(73), .type = AssetType::Material};
        const MetadataSerializer serializer;

        EXPECT_TRUE(serializer.save(metadata, sidecar_path));

        EXPECT_EQ(sidecar_path, directory.path() / "albedo.png.meta");
        EXPECT_EQ(serializer.load(sidecar_path).value(), metadata);
    }

    TEST(AssetMetadataTest, TextureMipmapSettingDefaultsOnAndRoundTripsOff) {
        const MetadataSerializer serializer;
        auto metadata = serializer.deserialize(R"({"version":3,"guid":42,"type":"texture",
            "importer":{"color_space":"srgb","flip_y":false}})");
        ASSERT_TRUE(metadata);
        auto& settings = std::get<TextureImportSettings>(metadata.value().import_settings);
        EXPECT_TRUE(settings.generate_mipmaps);
        settings.generate_mipmaps = false;
        settings.normal_map = true;
        const auto saved = serializer.serialize(metadata.value());
        ASSERT_TRUE(saved);
        EXPECT_EQ(serializer.deserialize(saved.value()).value(), metadata.value());
        EXPECT_FALSE(serializer.deserialize(R"({"version":3,"guid":42,"type":"texture",
            "importer":{"color_space":"srgb","flip_y":false,"generate_mipmaps":"false"}})"));
    }

    TEST(AssetMetadataTest, PreservesFullGuidRange) {
        const AssetMetadata metadata{
            .handle = AssetHandle(std::numeric_limits<std::uint64_t>::max()),
            .type = AssetType::Mesh};
        const MetadataSerializer serializer;

        EXPECT_EQ(serializer.deserialize(serializer.serialize(metadata).value()).value(), metadata);
    }

    TEST(AssetMetadataTest, SupportsDeclaredAssetTypes) {
        constexpr AssetType types[] = {AssetType::Texture, AssetType::Material, AssetType::Mesh,
            AssetType::Shader, AssetType::ShaderProgram, AssetType::Scene, AssetType::Audio};

        for(const AssetType type : types) {
            SCOPED_TRACE(std::string(to_string(type)));
            EXPECT_EQ(asset_type_from_string(to_string(type)), type);
        }
        EXPECT_FALSE(asset_type_from_string("unknown"));
        EXPECT_FALSE(asset_type_from_string("Texture"));
    }

    TEST(AssetMetadataTest, RejectsInvalidIdentityAndType) {
        const MetadataSerializer serializer;

        EXPECT_FALSE(serializer.deserialize(R"({"version": 3, "guid": 0, "type": "material"})"));
        EXPECT_FALSE(serializer.deserialize(R"({"version": 3, "guid": 42, "type": "video"})"));
        EXPECT_FALSE(serializer.serialize({.handle = INVALID_ASSET_HANDLE,
            .type = AssetType::Texture,
            .import_settings = TextureImportSettings{}}));
    }

    TEST(AssetMetadataTest, RejectsMalformedContract) {
        const MetadataSerializer serializer;

        EXPECT_FALSE(serializer.deserialize(R"({"version": 4, "guid": 42, "type": "material"})"));
        EXPECT_FALSE(serializer.deserialize(R"({"version": 3, "type": "material"})"));
        EXPECT_FALSE(serializer.deserialize(
            R"({"version": 3, "guid": 42, "type": "material", "extra": true})"));
    }

    TEST(AssetMetadataTest, ValidatesTextureImportSettings) {
        const MetadataSerializer serializer;

        EXPECT_FALSE(serializer.deserialize(R"({"version": 3, "guid": 42, "type": "texture"})"));
        EXPECT_FALSE(serializer.deserialize(R"({
  "version": 3,
  "guid": 42,
  "type": "texture",
  "importer": {"color_space": "display_p3", "flip_y": false}
})"));
        EXPECT_FALSE(serializer.deserialize(R"({
  "version": 3,
  "guid": 42,
  "type": "texture",
  "importer": {"color_space": "srgb", "flip_y": false, "compression": "high"}
})"));
        EXPECT_FALSE(serializer.deserialize(R"({
  "version": 3,
  "guid": 42,
  "type": "material",
  "importer": {"color_space": "srgb", "flip_y": false}
})"));
        EXPECT_FALSE(serializer.serialize({.handle = AssetHandle(42), .type = AssetType::Texture}));

        EXPECT_EQ(texture_color_space_from_string("srgb"), TextureColorSpace::Srgb);
        EXPECT_EQ(texture_color_space_from_string("linear"), TextureColorSpace::Linear);
        EXPECT_FALSE(texture_color_space_from_string("SRGB"));
    }

    TEST(AssetMetadataTest, RejectsQuotedBooleanAndDuplicateImporterField) {
        const MetadataSerializer serializer;
        const auto quoted = serializer.deserialize(R"({
  "version": 3, "guid": 42, "type": "texture",
  "importer": {"color_space": "srgb", "flip_y": "false"}
})");
        ASSERT_FALSE(quoted);
        EXPECT_NE(quoted.error().find("importer.flip_y"), std::string::npos);
        const auto duplicate = serializer.deserialize(R"({
  "version": 3, "guid": 42, "type": "texture",
  "importer": {"color_space": "srgb", "flip_y": false, "flip_y": true}
})");
        ASSERT_FALSE(duplicate);
        EXPECT_NE(duplicate.error().find("duplicate field 'flip_y'"), std::string::npos);
    }

    TEST(AssetMetadataTest, PreservesSourceAndNestedFieldDiagnostics) {
        const MetadataSerializer serializer;
        const auto missing = serializer.deserialize(R"({"version": 3})", "missing.meta");
        ASSERT_FALSE(missing);
        EXPECT_EQ(missing.error(),
            "Invalid asset metadata 'missing.meta' at '<root>': missing required field 'guid'");

        const auto duplicate = serializer.deserialize(
            R"({"version": 3, "guid": 42, "guid": 73, "type": "mesh"})", "duplicate.meta");
        ASSERT_FALSE(duplicate);
        EXPECT_EQ(duplicate.error(),
            "Invalid asset metadata 'duplicate.meta' at '<root>': duplicate field 'guid'");

        const auto scalar = serializer.deserialize(R"({
  "version": 3,
  "guid": 42,
  "type": "texture",
  "importer": {"color_space": "srgb", "flip_y": "not-a-bool"}
})",
            "scalar.meta");
        ASSERT_FALSE(scalar);
        EXPECT_EQ(scalar.error(),
            "Invalid asset metadata 'scalar.meta' at 'importer.flip_y': expected a boolean");

        const auto json = serializer.deserialize(R"({"version": [)", "syntax.meta");
        ASSERT_FALSE(json);
        EXPECT_TRUE(json.error().starts_with("Invalid asset metadata 'syntax.meta' at '<json>':"));
    }
}

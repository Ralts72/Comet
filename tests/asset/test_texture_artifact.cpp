#include "asset/artifact/texture_artifact.h"
#include "asset/import/import_service.h"
#include "asset/import/texture_importer.h"
#include "support/asset_manager_fixture.h"

namespace Comet::Tests {
    TEST(TextureArtifactTest, RoundTripLoadsMipChainWithoutTheSource) {
        const TemporaryProject project;
        const AssetHandle handle{93};
        const auto source = project.add_texture(handle);
        const auto inputs = capture_import_inputs(project.paths().assets(), source, {});
        ASSERT_TRUE(inputs);
        auto imported = TextureImporter{}.import(source);
        ASSERT_TRUE(imported);
        const TextureArtifact artifact{.handle = handle,
            .importer_version = TextureImporter::VERSION,
            .source = inputs.value().files.front(),
            .data = std::move(imported).value()};
        const auto path = project.paths().cache() / "texture.bin";
        ASSERT_TRUE(artifact.publish_atomic(path));
        std::filesystem::remove(source);
        const auto loaded = TextureArtifact::load(path, handle, artifact.data.pixels.size());
        ASSERT_TRUE(loaded);
        EXPECT_EQ(loaded->source, artifact.source);
        EXPECT_EQ(loaded->settings, artifact.settings);
        EXPECT_EQ(loaded->data.width, 512);
        EXPECT_EQ(loaded->data.height, 512);
        EXPECT_EQ(loaded->data.mip_levels, 10u);
        EXPECT_EQ(loaded->data.format, artifact.data.format);
        EXPECT_EQ(loaded->data.pixels, artifact.data.pixels);
        EXPECT_FALSE(TextureArtifact::load(path, handle, artifact.data.pixels.size() - 1));
        EXPECT_FALSE(TextureArtifact::load(path, AssetHandle{94}, artifact.data.pixels.size()));
        auto invalid = artifact;
        invalid.data.mip_levels = 1;
        EXPECT_FALSE(invalid.publish_atomic(path));
        ASSERT_TRUE(TextureArtifact::load(path, handle, artifact.data.pixels.size()));
        {
            std::fstream corrupt(path, std::ios::binary | std::ios::in | std::ios::out);
            corrupt.seekp(-1, std::ios::end);
            corrupt.put(char(artifact.data.pixels.back() ^ 0xff));
        }
        EXPECT_FALSE(TextureArtifact::load(path, handle, artifact.data.pixels.size()));
        std::filesystem::resize_file(path, 32);
        EXPECT_FALSE(TextureArtifact::load(path, handle, artifact.data.pixels.size()));
    }

    TEST(TextureArtifactTest, CacheReuseTracksSourceAndImportSettingsAndRepairsCorruption) {
        const TemporaryProject project;
        const AssetHandle handle{94};
        const auto source = project.add_texture(handle);
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().succeeded());
        const auto* record = database.find(handle);
        ASSERT_NE(record, nullptr);
        const ImportService imports(project.paths());
        constexpr auto budget = AssetImportLimits{}.texture_working_bytes;
        const auto path = imports.texture_artifact_path(handle);
        const auto prepare = [&](TextureImportSettings settings = {}) {
            return imports.prepare_texture(*record, settings, budget);
        };
        auto original = prepare();
        ASSERT_TRUE(original);
        const auto timestamp =
            std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
        std::filesystem::last_write_time(path, timestamp);
        ASSERT_TRUE(prepare());
        EXPECT_EQ(std::filesystem::last_write_time(path), timestamp);

        auto previous_version = TextureArtifact::load(path, handle, budget);
        ASSERT_TRUE(previous_version);
        --previous_version->importer_version;
        ASSERT_TRUE(previous_version->publish_atomic(path));
        std::filesystem::last_write_time(path, timestamp);
        ASSERT_TRUE(prepare());
        const auto regenerated = TextureArtifact::load(path, handle, budget);
        ASSERT_TRUE(regenerated);
        EXPECT_EQ(regenerated->importer_version, TextureImporter::VERSION);
        EXPECT_NE(std::filesystem::last_write_time(path), timestamp);

        const TextureImportSettings settings{
            .color_space = TextureColorSpace::Linear, .flip_y = true, .generate_mipmaps = false};
        auto changed = prepare(settings);
        ASSERT_TRUE(changed);
        EXPECT_EQ(changed.value().mip_levels, 1u);
        EXPECT_EQ(changed.value().format, Format::R8G8B8A8_UNORM);
        auto cached = TextureArtifact::load(path, handle, budget);
        ASSERT_TRUE(cached);
        EXPECT_EQ(cached->settings, settings);
        const TextureImportSettings normals{.normal_map = true};
        ASSERT_TRUE(prepare(normals));
        const auto normal_cache = TextureArtifact::load(path, handle, budget);
        ASSERT_TRUE(normal_cache);
        EXPECT_EQ(normal_cache->settings, normals);
        EXPECT_EQ(normal_cache->data.format, Format::R8G8B8A8_UNORM);
        ASSERT_TRUE(prepare(settings));
        const auto previous_source = cached->source;
        TemporaryProject::replace_texture(source);
        ASSERT_TRUE(prepare(settings));
        cached = TextureArtifact::load(path, handle, budget);
        ASSERT_TRUE(cached);
        EXPECT_NE(cached->source, previous_source);

        std::ofstream(path, std::ios::binary | std::ios::trunc) << "broken cache";
        ASSERT_TRUE(prepare(settings));
        ASSERT_TRUE(TextureArtifact::load(path, handle, budget));
        TemporaryProject::corrupt_texture(source);
        EXPECT_FALSE(prepare(settings));
    }
}

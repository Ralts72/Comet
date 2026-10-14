#include "assets/asset_thumbnails.h"
#include "asset/serialization/metadata_serializer.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <fstream>
#include <latch>

namespace CometEditor::Tests {
    using namespace Comet;

    TEST(AssetThumbnailTest, ResizePreservesAspectColorSpaceAndTransparentEdges) {
        TextureData source{.width = 256, .height = 128, .format = Format::R8G8B8A8_SRGB};
        source.pixels.resize(256 * 128 * 4);
        for(std::size_t i = 0; i < source.pixels.size(); i += 4) {
            const bool light = (i / 4) % 2;
            source.pixels[i] = source.pixels[i + 1] = source.pixels[i + 2] = light ? 255 : 0;
            source.pixels[i + 3] = 255;
        }
        auto result = AssetThumbnails::resize(source);
        ASSERT_TRUE(result) << result.error();
        EXPECT_EQ(result.value().width, 128);
        EXPECT_EQ(result.value().height, 64);
        EXPECT_EQ(result.value().mip_levels, 1u);
        EXPECT_EQ(result.value().pixels.size(), 128u * 64u * 4u);
        EXPECT_NEAR(result.value().pixels[0], 188, 1);
        source.format = Format::R8G8B8A8_UNORM;
        result = AssetThumbnails::resize(source);
        ASSERT_TRUE(result);
        EXPECT_NEAR(result.value().pixels[0], 128, 1);
        for(std::size_t i = 0; i < source.pixels.size(); i += 8)
            source.pixels[i + 3] = 0;
        result = AssetThumbnails::resize(source);
        ASSERT_TRUE(result);
        EXPECT_EQ(result.value().pixels[0], 255);
        EXPECT_EQ(result.value().pixels[3], 128);
        source.height = 1;
        source.pixels.resize(source.width * 4);
        result = AssetThumbnails::resize(source);
        ASSERT_TRUE(result);
        EXPECT_EQ(result.value().height, 1);
        source.pixels.clear();
        EXPECT_FALSE(AssetThumbnails::resize(source));
        EXPECT_FALSE(AssetThumbnails::resize({}));
    }

    class AssetThumbnailCacheTest: public ::testing::Test {
    protected:
        Comet::Tests::TemporaryDirectory directory;
        ProjectPaths paths{directory.path()};
        AssetDatabase database{paths};
        TaskScheduler scheduler{1};
        AssetThumbnails thumbnails{database, scheduler};
        std::filesystem::path sample = std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                       / "assets/textures/awesomeface.png";
        AssetHandle texture;
        void SetUp() override {
            std::filesystem::create_directories(paths.assets());
            std::filesystem::copy_file(sample, paths.assets() / "image.png");
            ASSERT_TRUE(database.scan().succeeded());
            const auto* record = database.find("image.png");
            ASSERT_NE(record, nullptr);
            texture = record->handle;
        }
        AssetThumbnails::Preview load(AssetHandle handle) {
            thumbnails.update();
            auto before = thumbnails.request(handle);
            scheduler.wait_idle();
            thumbnails.update();
            return thumbnails.request(handle);
        }
    };

    TEST_F(AssetThumbnailCacheTest, ReusesPixelsInvalidatesSettingsAndRecoversAfterSourceRepair) {
        auto original = load(texture);
        ASSERT_TRUE(original.image) << original.error;
        EXPECT_EQ(original.image->width, 128);
        EXPECT_EQ(thumbnails.request(texture).image, original.image);
        auto metadata = AssetMetadata{.handle = texture,
            .type = AssetType::Texture,
            .import_settings = TextureImportSettings{.flip_y = true}};
        ASSERT_TRUE(
            MetadataSerializer{}.save(metadata, metadata_path(paths.assets() / "image.png")));
        ASSERT_TRUE(database.scan().succeeded());
        thumbnails.update();
        EXPECT_FALSE(thumbnails.request(texture).image);
        auto flipped = load(texture);
        ASSERT_TRUE(flipped.image) << flipped.error;
        EXPECT_NE(flipped.image, original.image);
        EXPECT_NE(flipped.image->pixels, original.image->pixels);
        std::ofstream(paths.assets() / "image.png", std::ios::trunc) << "broken image";
        ASSERT_TRUE(database.scan().succeeded());
        auto failed = load(texture);
        EXPECT_FALSE(failed.image);
        EXPECT_FALSE(failed.error.empty());
        EXPECT_EQ(thumbnails.request(texture).error, failed.error);
        std::filesystem::copy_file(sample, paths.assets() / "image.png",
            std::filesystem::copy_options::overwrite_existing);
        ASSERT_TRUE(database.scan().succeeded());
        auto repaired = load(texture);
        ASSERT_TRUE(repaired.image) << repaired.error;
        EXPECT_EQ(repaired.image->pixels, flipped.image->pixels);
        std::filesystem::remove(paths.assets() / "image.png");
        std::filesystem::remove(metadata_path(paths.assets() / "image.png"));
        ASSERT_TRUE(database.scan().succeeded());
        thumbnails.update();
        EXPECT_FALSE(thumbnails.request(texture).image);
        EXPECT_EQ(original.image->width, 128);
    }

    TEST_F(AssetThumbnailCacheTest, DiscardsAQueuedResultWhenSourceRevisionChanges) {
        std::latch entered{1}, release{1};
        auto blocker = scheduler.try_submit([&] {
            entered.count_down();
            release.wait();
        });
        ASSERT_TRUE(blocker);
        entered.wait();
        EXPECT_FALSE(thumbnails.request(texture).image);
        std::ofstream(paths.assets() / "image.png", std::ios::trunc) << "broken image";
        const auto report = database.scan();
        release.count_down();
        scheduler.wait_idle();
        ASSERT_TRUE(report.succeeded());
        thumbnails.update();
        const auto next = thumbnails.request(texture);
        EXPECT_FALSE(next.image);
        EXPECT_TRUE(next.error.empty());
        scheduler.wait_idle();
        thumbnails.update();
        EXPECT_FALSE(thumbnails.request(texture).error.empty());
    }

    TEST_F(AssetThumbnailCacheTest, BoundsCacheAndReleasesLeastRecentlyUsedPixels) {
        std::weak_ptr<const TextureData> oldest;
        {
            auto first = load(texture);
            ASSERT_TRUE(first.image);
            oldest = first.image;
        }
        for(std::size_t i = 0; i < AssetThumbnails::CACHE_SIZE; ++i) {
            const auto name = std::to_string(i) + ".png";
            std::filesystem::copy_file(sample, paths.assets() / name);
            ASSERT_TRUE(database.scan().succeeded());
            auto preview = load(database.find(name)->handle);
            ASSERT_TRUE(preview.image) << preview.error;
        }
        EXPECT_TRUE(oldest.expired());
    }
}

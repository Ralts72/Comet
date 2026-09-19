#include "asset/import/import_service.h"
#include "asset/import/environment_importer.h"
#include "support/temporary_directory.h"
#include "support/hdr_image.h"

#include <gtest/gtest.h>
#include <fstream>

namespace Comet::Tests {
    class EnvironmentArtifactTest: public ::testing::Test {
    protected:
        TemporaryDirectory directory;
        ProjectPaths paths{directory.path()};
        ImportService imports{paths};
        AssetRecord record{
            .handle = AssetHandle(123), .type = AssetType::Environment, .path = "studio.hdr"};

        void SetUp() override {
            std::filesystem::create_directories(paths.assets());
            write_hdr(paths.assets() / record.path);
        }
        Result<EnvironmentArtifact> prepare() {
            return imports.prepare_environment(record, EnvironmentImporter::MAX_WORKING_BYTES);
        }
    };

    TEST_F(EnvironmentArtifactTest, ReusesMatchingInputAndVersionWithoutRewritingCache) {
        auto first = prepare();
        ASSERT_TRUE(first) << first.error();
        const auto cache = imports.environment_artifact_path(record.handle);
        const auto stamp = std::filesystem::last_write_time(cache);
        auto second = prepare();
        ASSERT_TRUE(second) << second.error();
        EXPECT_EQ(first.value().data.background.pixels, second.value().data.background.pixels);
        EXPECT_EQ(first.value().data.irradiance.pixels, second.value().data.irradiance.pixels);
        EXPECT_EQ(first.value().data.specular.pixels, second.value().data.specular.pixels);
        EXPECT_EQ(first.value().data.brdf.pixels, second.value().data.brdf.pixels);
        EXPECT_EQ(std::filesystem::last_write_time(cache), stamp);
        EXPECT_FALSE(EnvironmentArtifact::load(cache, AssetHandle(124), 1024));
        EXPECT_FALSE(EnvironmentArtifact::load(cache, record.handle, 1));
    }

    TEST_F(EnvironmentArtifactTest, RebuildsCorruptionAlgorithmChangesAndChangedSources) {
        auto first = prepare();
        ASSERT_TRUE(first);
        const auto cache = imports.environment_artifact_path(record.handle);
        ++first.value().importer_version;
        ASSERT_TRUE(first.value().publish_atomic(cache));
        auto current = prepare();
        ASSERT_TRUE(current);
        EXPECT_EQ(current.value().importer_version, EnvironmentImporter::VERSION);
        std::ofstream(cache, std::ios::binary | std::ios::trunc) << "broken";
        ASSERT_TRUE(prepare());
        write_hdr(paths.assets() / record.path, 8, 4);
        auto changed = prepare();
        ASSERT_TRUE(changed);
        EXPECT_NE(changed.value().source, first.value().source);
        EXPECT_EQ(changed.value().data.background.width, 2);
    }

    TEST_F(EnvironmentArtifactTest, FailedSourceDoesNotReplaceLastValidArtifact) {
        auto first = prepare();
        ASSERT_TRUE(first);
        std::ofstream(paths.assets() / record.path) << "invalid HDR";
        EXPECT_FALSE(prepare());
        auto preserved = EnvironmentArtifact::load(imports.environment_artifact_path(record.handle),
            record.handle, EnvironmentImporter::MAX_WORKING_BYTES);
        ASSERT_TRUE(preserved);
        EXPECT_EQ(preserved->source, first.value().source);
        EXPECT_EQ(preserved->data.background.pixels, first.value().data.background.pixels);
    }

    TEST_F(EnvironmentArtifactTest, RejectsPayloadCorruptionAndTrailingBytes) {
        ASSERT_TRUE(prepare());
        const auto cache = imports.environment_artifact_path(record.handle);
        {
            std::fstream file(cache, std::ios::in | std::ios::out | std::ios::binary);
            file.seekg(-1, std::ios::end);
            const char changed = static_cast<char>(file.get() ^ 1);
            file.seekp(-1, std::ios::end);
            file.put(changed);
        }
        EXPECT_FALSE(EnvironmentArtifact::load(
            cache, record.handle, EnvironmentImporter::MAX_WORKING_BYTES));
        ASSERT_TRUE(prepare());
        std::ofstream(cache, std::ios::app | std::ios::binary).put('x');
        EXPECT_FALSE(EnvironmentArtifact::load(
            cache, record.handle, EnvironmentImporter::MAX_WORKING_BYTES));
    }
}

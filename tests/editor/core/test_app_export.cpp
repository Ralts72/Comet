#include "project/app_export.h"
#include "support/temporary_directory.h"
#include "common/file_io.h"
#include <gtest/gtest.h>

#ifdef __APPLE__
namespace CometEditor::Tests {
    TEST(AppExportTest, PassesLiteralPathsAndReturnsToolFailureWithoutReplacingOutput) {
        Comet::Tests::TemporaryDirectory temporary;
        const auto destination = temporary.path() / "中文 'quoted' $HOME.app";
        std::filesystem::create_directory(destination);
        const auto marker = destination / "preserve.txt";
        ASSERT_TRUE(Comet::write_text_file_atomic(marker, "previous export"));
        const AppExportRequest request{
            COMET_SAMPLE_PROJECT_DIRECTORY, destination, {"scenes/character_second.scene"}};
        std::string output;
        const auto existing =
            export_app(request, [&](std::string_view text) { output.append(text); });
        ASSERT_FALSE(existing);
        EXPECT_NE(existing.error().find("Destination already exists"), std::string::npos);
        EXPECT_NE(existing.error().find(destination.string()), std::string::npos);
        EXPECT_EQ(std::filesystem::file_size(marker), 15);
        EXPECT_NE(output.find("[Comet export]"), std::string::npos);
        EXPECT_NE(output.find("Destination already exists"), std::string::npos);

        auto missing = request;
        missing.destination = temporary.path() / "New.app";
        missing.project = temporary.path() / "missing project";
        const auto failed = export_app(missing);
        ASSERT_FALSE(failed);
        EXPECT_NE(failed.error().find("Project file not found"), std::string::npos);
        EXPECT_FALSE(std::filesystem::exists(missing.destination));
    }
}
#endif

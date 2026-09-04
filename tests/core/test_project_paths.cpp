#include "core/project_paths.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace Comet::Tests {
    TEST(ProjectPathsTest, ResolvesStandardProjectDirectories) {
        const ProjectPaths paths(std::filesystem::path("Projects") / "Sandbox");

        EXPECT_EQ(paths.root(), std::filesystem::path("Projects/Sandbox"));
        EXPECT_EQ(paths.assets(), std::filesystem::path("Projects/Sandbox/assets"));
        EXPECT_EQ(paths.local_data(), std::filesystem::path("Projects/Sandbox/.comet"));
        EXPECT_EQ(paths.cache(), std::filesystem::path("Projects/Sandbox/.comet/cache"));
        EXPECT_EQ(paths.editor_state(),
            std::filesystem::path("Projects/Sandbox/.comet/editor"));
        EXPECT_EQ(
            paths.settings(), std::filesystem::path("Projects/Sandbox/ProjectSettings"));
    }

    TEST(ProjectPathsTest, NormalizesRootWithoutAccessingFileSystem) {
        const ProjectPaths paths(
            std::filesystem::path("Projects") / "Draft" / ".." / "Sandbox");

        EXPECT_EQ(paths.root(), std::filesystem::path("Projects/Sandbox"));
        EXPECT_EQ(paths.assets(), paths.root() / "assets");
    }
}

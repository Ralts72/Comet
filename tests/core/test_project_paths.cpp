#include "core/project_paths.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace Comet::Tests {
    TEST(ProjectPathsTest, ResolvesStandardProjectDirectories) {
        const ProjectPaths paths(std::filesystem::path("Projects") / "Sandbox");

        EXPECT_EQ(paths.root(), std::filesystem::path("Projects/Sandbox"));
        EXPECT_EQ(paths.assets(), std::filesystem::path("Projects/Sandbox/Assets"));
        EXPECT_EQ(paths.library(), std::filesystem::path("Projects/Sandbox/Library"));
        EXPECT_EQ(paths.settings(), std::filesystem::path("Projects/Sandbox/ProjectSettings"));
    }

    TEST(ProjectPathsTest, NormalizesRootWithoutAccessingFileSystem) {
        const ProjectPaths paths(std::filesystem::path("Projects") / "Draft" / ".." / "Sandbox");

        EXPECT_EQ(paths.root(), std::filesystem::path("Projects/Sandbox"));
        EXPECT_EQ(paths.assets(), paths.root() / "Assets");
    }
}

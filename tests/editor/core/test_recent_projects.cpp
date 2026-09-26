#include "project/recent_projects.h"

#include "common/file_io.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <string>

namespace CometEditor::Tests {
    TEST(RecentProjectsTest, PersistsMostRecentFirstWithoutDuplicates) {
        Comet::Tests::TemporaryDirectory directory;
        const auto state = directory.path() / "editor/recent-projects.json";
        auto loaded = RecentProjects::load(state);
        ASSERT_TRUE(loaded) << loaded.error();
        auto recent = std::move(loaded).value();
        for(int index = 0; index < 12; ++index) {
            const auto root = directory.path() / ("project-" + std::to_string(index));
            ASSERT_TRUE(std::filesystem::create_directory(root));
            ASSERT_TRUE(recent.record(root));
        }
        ASSERT_EQ(recent.entries().size(), 10U);
        EXPECT_EQ(recent.entries().front(), directory.path() / "project-11");
        EXPECT_EQ(recent.entries().back(), directory.path() / "project-2");

        ASSERT_TRUE(recent.record(directory.path() / "project-5"));
        EXPECT_EQ(recent.entries().front(), directory.path() / "project-5");
        auto reopened = RecentProjects::load(state);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().entries(), recent.entries());
        ASSERT_TRUE(recent.record(directory.path() / "project-5"));
        EXPECT_EQ(RecentProjects::load(state).value().entries(), recent.entries());
    }

    TEST(RecentProjectsTest, RejectsInvalidStateWithoutRewritingIt) {
        Comet::Tests::TemporaryDirectory directory;
        const auto state = directory.path() / "recent-projects.json";
        for(const std::string content : {"{", R"({"version":2,"projects":[]})",
                 R"({"version":1,"projects":["relative/project"]})"}) {
            ASSERT_TRUE(Comet::write_text_file_atomic(state, content));
            EXPECT_FALSE(RecentProjects::load(state));
            EXPECT_EQ(Comet::read_text_file(state).value(), content);
        }
    }
}

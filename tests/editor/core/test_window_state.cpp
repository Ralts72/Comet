#include "ui/window_state.h"

#include "common/file_io.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

namespace CometEditor::Tests {
    TEST(WindowStateTest, MissingStateKeepsConfiguredDefaultsAndSavedStateOverridesOnlyPlacement) {
        Comet::Tests::TemporaryDirectory directory;
        const auto path = directory.path() / "editor/window.json";
        const auto missing = WindowState::load(path);
        ASSERT_TRUE(missing) << missing.error();
        EXPECT_FALSE(missing.value());

        const WindowState state{1120, 840, true};
        ASSERT_TRUE(state.save(path));
        const auto loaded = WindowState::load(path);
        ASSERT_TRUE(loaded) << loaded.error();
        ASSERT_TRUE(loaded.value());
        Comet::WindowSettings settings{.width = 960, .height = 720, .title = "Editor"};
        loaded.value()->apply_to(settings);
        EXPECT_EQ(settings.width, 1120);
        EXPECT_EQ(settings.height, 840);
        EXPECT_TRUE(settings.maximized);
        EXPECT_EQ(settings.title, "Editor");
        EXPECT_TRUE(settings.resizable);
        EXPECT_EQ(settings.mode, Comet::WindowMode::Windowed);

        settings.mode = Comet::WindowMode::Fullscreen;
        settings.width = 1920;
        settings.height = 1080;
        settings.maximized = false;
        loaded.value()->apply_to(settings);
        EXPECT_EQ(settings.width, 1920);
        EXPECT_EQ(settings.height, 1080);
        EXPECT_FALSE(settings.maximized);
    }

    TEST(WindowStateTest, RejectsMalformedOrInvalidDimensionsWithoutRewritingState) {
        Comet::Tests::TemporaryDirectory directory;
        const auto path = directory.path() / "window.json";
        for(const std::string contents : {"{", R"({"width":0,"height":720,"maximized":false})",
                R"({"width":960,"height":-1,"maximized":false})",
                R"({"width":2147483648,"height":720,"maximized":false})",
                R"({"width":960,"height":720,"maximized":1})"}) {
            SCOPED_TRACE(contents);
            ASSERT_TRUE(Comet::write_text_file_atomic(path, contents));
            EXPECT_FALSE(WindowState::load(path));
            EXPECT_EQ(Comet::read_text_file(path).value(), contents);
        }
        EXPECT_FALSE((WindowState{0, 720, false}.save(path)));
        EXPECT_EQ(
            Comet::read_text_file(path).value(), R"({"width":960,"height":720,"maximized":1})");
    }
}

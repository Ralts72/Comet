#include "ui/resource_dependencies.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    class UiResourceDependenciesTest: public testing::Test {
    protected:
        TemporaryDirectory directory;
        Project::UiEntry entry{"ui/menu.rml", "ui/menu.ui.lua"};

        void write(const std::filesystem::path& path, std::string_view text) {
            ASSERT_TRUE(write_text_file_atomic(directory.path() / path, text));
        }
        void SetUp() override {
            write(entry.controller, "return {}");
            write(entry.document, R"(<rml><head><link type="text/rcss" href="theme.rcss" /></head>
                <body><div style="display: none"><img src="../images/icon.png" /></div></body></rml>)");
            write("ui/theme.rcss", "body { color: white; }");
            write("images/icon.png", "image bytes");
        }
        Result<std::vector<std::filesystem::path>> collect() const {
            return Ui::collect_resource_dependencies(directory.path(), entry);
        }
    };

    TEST_F(
        UiResourceDependenciesTest, CollectsTemplatesStylesHiddenImagesDecoratorsAndProjectFonts) {
        write(entry.document, R"rml(<rml><head>
            <LINK type="TEXT/CSS" href="theme.rcss" />
            <link type="text/template" href="templates/panel.rml" />
            <style>p:hover { decorator: image('../images/hover icon.png'); }</style>
            </head><body><div style="display: none"><img src="../images/icon.png" /></div>
            <p style="decorator: image(badge)">Sprite</p></body></rml>)rml");
        write("ui/templates/panel.rml", R"(<template name="panel" content="content"><head>
            <link type="text/rcss" href="../sprites.rcss" /></head>
            <body><div id="content"><img src="../images/icon.png" /></div></body></template>)");
        write("ui/theme.rcss", R"(
            /* .unused { decorator: image(missing.png); } */
            @font-face { font-family: Project; src: fonts/custom.ttf, fonts/fallback font.otf; }
            @media (min-width: 2000px) { .hidden { decorator: image('../images/large.png' cover); } }
            @decorator panel : tiled-box {
                top-left-image-src: badge;
                center-image: '../images/icon.png' repeat;
            }
            .panel { decorator: tiled-horizontal(auto, '../images/icon.png' repeat-x, badge); }
        )");
        write("ui/sprites.rcss", R"(@spritesheet icons {
            src: ../images/atlas.png;
            resolution: 1x;
            badge: 0px 0px 8px 8px;
        })");
        for(const auto* path : {"images/atlas.png", "images/hover icon.png", "images/large.png",
                "fonts/custom.ttf", "fonts/fallback font.otf", "unrelated.txt"})
            write(path, "resource bytes");
        const auto resources = collect();
        ASSERT_TRUE(resources) << resources.error();
        const std::vector<std::filesystem::path> expected{"fonts/custom.ttf",
            "fonts/fallback font.otf", "images/atlas.png", "images/hover icon.png",
            "images/icon.png", "images/large.png", "ui/menu.rml", "ui/menu.ui.lua",
            "ui/sprites.rcss", "ui/templates/panel.rml", "ui/theme.rcss"};
        EXPECT_EQ(resources.value(), expected);
        EXPECT_EQ(collect().value(), expected);
        EXPECT_FALSE(std::filesystem::exists(directory.path() / "ui/menu.rml.meta"));
    }

    TEST_F(
        UiResourceDependenciesTest, MissingReferencesIdentifyTheOwningFileAndKeepNoCachedClosure) {
        write("ui/theme.rcss", ".unused:hover { decorator: image('../images/missing.png'); }");
        auto resources = collect();
        ASSERT_FALSE(resources);
        EXPECT_NE(resources.error().find("ui/theme.rcss"), std::string::npos);
        EXPECT_NE(resources.error().find("../images/missing.png"), std::string::npos);
        write("images/missing.png", "image bytes");
        ASSERT_TRUE(collect());
        write("ui/theme.rcss", "body { color: white; }");
        resources = collect();
        ASSERT_TRUE(resources);
        EXPECT_EQ(resources.value(), (std::vector<std::filesystem::path>{"images/icon.png",
                                         "ui/menu.rml", "ui/menu.ui.lua", "ui/theme.rcss"}));
        std::filesystem::remove(directory.path() / "images/icon.png");
        resources = collect();
        ASSERT_FALSE(resources);
        EXPECT_NE(resources.error().find("ui/menu.rml"), std::string::npos);
        EXPECT_NE(resources.error().find("icon.png"), std::string::npos);
    }

    TEST_F(
        UiResourceDependenciesTest, MissingTemplateStyleControllerOrFontFailsBeforeRuntimeLoading) {
        for(const auto* text :
            {R"(<rml><head><link type="text/template" href="missing.rml" /></head></rml>)",
                R"(<rml><head><link type="text/rcss" href="missing.rcss" /></head></rml>)",
                R"(<rml><head><style>@font-face { font-family: X; src: fonts/missing.ttf; }</style></head></rml>)"}) {
            write(entry.document, text);
            const auto resources = collect();
            EXPECT_FALSE(resources);
            if(!resources)
                EXPECT_NE(resources.error().find("missing"), std::string::npos);
        }
        write(entry.document, "<rml><body /></rml>");
        std::filesystem::remove(directory.path() / entry.controller);
        const auto resources = collect();
        ASSERT_FALSE(resources);
        EXPECT_NE(resources.error().find("menu.ui.lua"), std::string::npos);
    }

    TEST_F(UiResourceDependenciesTest, RejectsExternalFilesDirectoriesAndEscapingEntryPaths) {
        TemporaryDirectory external;
        ASSERT_TRUE(write_text_file_atomic(external.path() / "outside.png", "image bytes"));
        for(const auto& source :
            {std::string("../../outside.png"), std::string("https://example.test/icon.png"),
                (external.path() / "outside.png").generic_string(), std::string("../images")}) {
            SCOPED_TRACE(source);
            write(entry.document, "<rml><body><img src=\"" + source + "\" /></body></rml>");
            EXPECT_FALSE(collect());
        }
        entry.document = "ui/../ui/menu.rml";
        EXPECT_FALSE(collect());
    }

    TEST_F(UiResourceDependenciesTest, ResolvesSymlinksWithoutDroppingTheReferencedExportPath) {
        std::error_code error;
        std::filesystem::create_symlink(
            directory.path() / "images/icon.png", directory.path() / "ui/alias.png", error);
        if(error)
            GTEST_SKIP() << "Symlinks unavailable: " << error.message();
        write(entry.document, "<rml><body><img src=\"alias.png\" /></body></rml>");
        const auto resources = collect();
        ASSERT_TRUE(resources);
        EXPECT_EQ(resources.value(),
            (std::vector<std::filesystem::path>{"ui/alias.png", "ui/menu.rml", "ui/menu.ui.lua"}));
        TemporaryDirectory external;
        ASSERT_TRUE(write_text_file_atomic(external.path() / "outside.png", "image bytes"));
        std::filesystem::remove(directory.path() / "ui/alias.png");
        std::filesystem::create_symlink(
            external.path() / "outside.png", directory.path() / "ui/alias.png", error);
        ASSERT_FALSE(error);
        EXPECT_FALSE(collect());
    }
}

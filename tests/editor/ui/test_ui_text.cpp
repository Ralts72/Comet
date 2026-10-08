#ifdef COMET_TEST_EDITOR_UI
#include "ui/text.h"
#include "support/imgui_context.h"
#include "support/temporary_directory.h"
#include "common/file_io.h"

#include <gtest/gtest.h>
#include <imgui_internal.h>

namespace Comet::Tests {
    using namespace CometEditor;

    TEST(EditorTranslationTest, RejectsInvalidMappingsAndPlaceholderChanges) {
        constexpr std::string_view invalid[]{"", "File: [", "[File]", "File: null", "File: []",
            "File: {}", "File: 42", "File: true", "42: value", "File: !!int 2",
            "File: 文件\nFile: 重复", "File: \"\"", "\"\": 空键", R"(File: "文\0件")",
            R"("%s %zu": "%zu %s")", R"("Value %s": "值 %d")", R"("Value %zu": "值 %u")",
            R"("Value %*.*f": "值 %f")", R"("Value %.3f": "值 %n")", R"("Value %.3f": "值 %")",
            R"("File": "文件 %s")"};
        for(const auto yaml : invalid) {
            SCOPED_TRACE(yaml);
            EXPECT_FALSE(Ui::parse_translations(yaml));
        }
    }

    TEST(EditorTranslationTest, PreservesQuotedStringsMultilineTextAndFormatArguments) {
        auto parsed = Ui::parse_translations(R"(
"Value %*.*f / %zu / %llu / %s / %%": "值 %*.*f / %zu / %llu / %s / %%"
"Line\nnext": "行\n下一行"
"True": "true"
File: 文件
)");
        ASSERT_TRUE(parsed) << parsed.error();
        const Ui::TextScope text(parsed.value());
        EXPECT_STREQ(
            Ui::text("Value %*.*f / %zu / %llu / %s / %%"), "值 %*.*f / %zu / %llu / %s / %%");
        EXPECT_STREQ(Ui::text("Line\nnext"), "行\n下一行");
        EXPECT_STREQ(Ui::text("True"), "true");
        EXPECT_STREQ(Ui::text("File"), "文件");
        EXPECT_STREQ(Ui::text("Unknown custom property"), "Unknown custom property");
    }

    TEST(EditorTranslationTest, OwnsLoadedTextAndReportsCatalogErrors) {
        TemporaryDirectory directory;
        const auto path = directory.path() / "zh-CN.yaml";
        const Ui::Translations empty;
        const Ui::TextScope fallback(empty);
        EXPECT_FALSE(Ui::load_translations(path));
        EXPECT_STREQ(Ui::text("File"), "File");
        ASSERT_TRUE(write_text_file_atomic(path, "File: 文件"));
        auto first = Ui::load_translations(path);
        ASSERT_TRUE(first) << first.error();
        {
            const Ui::TextScope first_scope(first.value());
            ASSERT_TRUE(write_text_file_atomic(path, "File: 修改后的文字"));
            EXPECT_STREQ(Ui::text("File"), "文件");
            auto second = Ui::load_translations(path);
            ASSERT_TRUE(second) << second.error();
            {
                const Ui::TextScope second_scope(second.value());
                EXPECT_STREQ(Ui::text("File"), "修改后的文字");
            }
            EXPECT_STREQ(Ui::text("File"), "文件");
            ASSERT_TRUE(write_text_file_atomic(path, "File: 42"));
            const auto invalid = Ui::load_translations(path);
            ASSERT_FALSE(invalid);
            EXPECT_NE(invalid.error().find(path.string()), std::string::npos);
            EXPECT_STREQ(Ui::text("File"), "文件");
        }
        EXPECT_STREQ(Ui::text("File"), "File");
    }

    class EditorTextTest: public testing::Test {
    protected:
        void SetUp() override {
            auto loaded = Ui::load_translations();
            ASSERT_TRUE(loaded) << loaded.error();
            translations = std::move(loaded).value();
        }
        Ui::Translations translations;
    };

    TEST_F(EditorTextTest, TranslationKeepsControlIdentityAndUnknownText) {
        ImGuiTestContext imgui;
        const std::string custom = "custom_shader_parameter";
        EXPECT_STREQ(Ui::text("Inspector"), "Inspector");
        {
            const Ui::TextScope text(translations);
            EXPECT_STREQ(Ui::text("Inspector"), "属性");
            EXPECT_STREQ(Ui::text("Rigid Body"), "刚体");
            EXPECT_STREQ(Ui::text("Collider"), "碰撞体");
            EXPECT_STREQ(Ui::text("Box Half Extents"), "盒体半尺寸");
            EXPECT_STREQ(Ui::text("Dynamic"), "动态");
            EXPECT_EQ(ImHashStr(Ui::label("Inspector").c_str()), ImHashStr("Inspector"));
            EXPECT_EQ(
                ImHashStr(Ui::label("Near Clip").c_str(), 0, 123), ImHashStr("Near Clip", 0, 123));
            EXPECT_EQ(Ui::label(custom.c_str()), custom);
        }
        EXPECT_STREQ(Ui::text("Inspector"), "Inspector");
    }

    TEST_F(EditorTextTest, ChineseLabelsReuseWindowAndCollapsingHeaderState) {
        ImGuiTestContext imgui;
        const auto draw = [&] {
            ImGui::NewFrame();
            ImGui::Begin(Ui::label("Inspector").c_str());
            const bool open = ImGui::CollapsingHeader(Ui::label("Camera").c_str());
            ImGui::End();
            ImGui::Render();
            return open;
        };
        draw();
        auto* window = ImGui::FindWindowByName("Inspector");
        ASSERT_NE(window, nullptr);
        window->StateStorage.SetInt(window->GetID("Camera"), 1);
        const Ui::TextScope text(translations);
        EXPECT_TRUE(draw());
        EXPECT_EQ(ImGui::FindWindowByName("Inspector"), window);
        EXPECT_TRUE(draw());
    }
}
#endif

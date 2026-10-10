#include "support/imgui_context.h"
#include "ui/icons.h"

#include <gtest/gtest.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace CometEditor::Tests {
    TEST(IconAtlasTest, GrowthKeepsImagesAndButtonsPreserveTheirIdsAndDisabledState) {
        Comet::Tests::ImGuiTestContext context;
        Ui::IconAtlas icons;
        auto& atlas = *ImGui::GetIO().Fonts;
        const auto loaded = icons.load(
            atlas, std::filesystem::path(PROJECT_ROOT_DIR) / "editor/resources/icons/ui");
        ASSERT_TRUE(loaded) << loaded.error();

        unsigned char* pixels;
        int width, height;
        const auto build = [&] { atlas.GetTexDataAsRGBA32(&pixels, &width, &height); };
        build();
        ImGuiID button_id = 0;
        std::vector<std::uint32_t> before;
        const auto frame = [&](bool disabled) {
            ImGui::NewFrame();
            ImGui::Begin("Icon buttons");
            button_id = ImGui::GetID("Play");
            ImGui::BeginDisabled(disabled);
            const bool pressed = Ui::icon_button(Ui::Icon::Play, "Run###Play");
            EXPECT_EQ(GImGui->LastItemData.ID, button_id);
            auto& vertices = ImGui::GetWindowDrawList()->VtxBuffer;
            const auto uv0 = vertices[vertices.Size - 4].uv;
            const auto uv1 = vertices[vertices.Size - 2].uv;
            const int left = int(std::lround(uv0.x * atlas.TexData->Width));
            const int top = int(std::lround(uv0.y * atlas.TexData->Height));
            const int right = int(std::lround(uv1.x * atlas.TexData->Width));
            const int bottom = int(std::lround(uv1.y * atlas.TexData->Height));
            std::vector<std::uint32_t> image;
            for(int y = top; y < bottom; ++y) {
                for(int x = left; x < right; ++x) {
                    std::uint32_t pixel;
                    std::memcpy(&pixel, atlas.TexData->GetPixelsAt(x, y), sizeof(pixel));
                    image.push_back(pixel);
                }
            }
            if(before.empty())
                before = image;
            else
                EXPECT_EQ(image, before);
            ImGui::EndDisabled();
            ImGui::End();
            ImGui::Render();
            return pressed;
        };
        EXPECT_FALSE(frame(false));
        ASSERT_FALSE(before.empty());
        EXPECT_TRUE(std::ranges::any_of(
            before, [](std::uint32_t pixel) { return (pixel & IM_COL32_A_MASK) != 0; }));
        const int old_width = atlas.TexData->Width;
        ASSERT_NE(atlas.AddCustomRect(old_width + 1, 64), ImFontAtlasRectId_Invalid);
        build();
        EXPECT_GT(atlas.TexData->Width, old_width);
        ImGui::ActivateItemByID(button_id);
        EXPECT_TRUE(frame(false));
        ImGui::ActivateItemByID(button_id);
        EXPECT_FALSE(frame(true));
    }
}

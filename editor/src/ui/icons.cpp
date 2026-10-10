#include "ui/icons.h"
#include "asset/import/texture_importer.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cstring>
#include <string>

namespace CometEditor::Ui {
    Comet::Result<void> IconAtlas::load(
        ImFontAtlas& atlas, const std::filesystem::path& directory) {
        constexpr std::array files{"play.png", "stop.png", "pause.png", "step.png", "reload.png",
            "settings.png", "input.png"};
        static_assert(files.size() == static_cast<std::size_t>(Icon::Count));
        for(std::size_t index = 0; index < files.size(); ++index) {
            auto imported =
                Comet::TextureImporter{}.import(directory / files[index], {.flip_y = false});
            if(!imported)
                return Comet::Result<void>::failure(imported.error());
            const auto& image = imported.value();
            Region region{.left = image.width, .top = image.height};
            for(int y = 0; y < image.height; ++y) {
                for(int x = 0; x < image.width; ++x) {
                    if(image.pixels[(std::size_t(y) * image.width + x) * 4 + 3] == 0)
                        continue;
                    region.left = std::min(region.left, x);
                    region.top = std::min(region.top, y);
                    region.right = std::max(region.right, x + 1);
                    region.bottom = std::max(region.bottom, y + 1);
                }
            }
            if(region.right == 0 || region.bottom == 0)
                return Comet::Result<void>::failure(
                    "Editor icon is empty: " + std::string(files[index]));
            region.left = std::max(0, region.left - 1);
            region.top = std::max(0, region.top - 1);
            region.right = std::min(image.width, region.right + 1);
            region.bottom = std::min(image.height, region.bottom + 1);

            ImFontAtlasRect rectangle;
            region.id = atlas.AddCustomRect(image.width, image.height, &rectangle);
            if(region.id == ImFontAtlasRectId_Invalid)
                return Comet::Result<void>::failure(
                    "Cannot pack editor icon: " + std::string(files[index]));
            for(int y = 0; y < image.height; ++y) {
                std::memcpy(atlas.TexData->GetPixelsAt(rectangle.x, rectangle.y + y),
                    image.pixels.data() + std::size_t(y) * image.width * 4,
                    std::size_t(image.width) * 4);
            }
            m_regions[index] = region;
        }
        atlas.TexPixelsUseColors = true;
        // 字体图集借用所属 ImGuiContext 的图标表，不增加全局图标缓存。
        atlas.UserData = this;
        return Comet::Result<void>::success();
    }

    void IconAtlas::draw(ImFontAtlas& atlas, const Icon icon, const ImVec2 position,
        const float size, const ImU32 color) const {
        const auto& region = m_regions[static_cast<std::size_t>(icon)];
        ImFontAtlasRect rectangle;
        if(!atlas.GetCustomRect(region.id, &rectangle))
            return;
        const float width = float(region.right - region.left);
        const float height = float(region.bottom - region.top);
        const float scale = size / std::max(width, height);
        const ImVec2 start{position.x + (size - width * scale) * 0.5f,
            position.y + (size - height * scale) * 0.5f};
        const ImVec2 end{start.x + width * scale, start.y + height * scale};
        // 中文字形按需烘焙会移动矩形；每次绘制读取当前纹理和 UV。
        const ImVec2 uv0{float(rectangle.x + region.left) / atlas.TexData->Width,
            float(rectangle.y + region.top) / atlas.TexData->Height};
        const ImVec2 uv1{float(rectangle.x + region.right) / atlas.TexData->Width,
            float(rectangle.y + region.bottom) / atlas.TexData->Height};
        ImGui::GetWindowDrawList()->AddImage(atlas.TexRef, start, end, uv0, uv1, color);
    }

    bool icon_button(const Icon icon, const char* label, ImVec2 size) {
        if(ImGui::GetCurrentWindow()->SkipItems)
            return false;
        auto& atlas = *ImGui::GetIO().Fonts;
        const auto* icons = static_cast<const IconAtlas*>(atlas.UserData);
        if(!icons)
            return ImGui::Button(label, size);
        const float icon_size = ImGui::GetFontSize();
        const auto& style = ImGui::GetStyle();
        const auto text_size = ImGui::CalcTextSize(label, nullptr, true);
        const float content_width = icon_size + style.ItemInnerSpacing.x + text_size.x;
        size.x = std::max(size.x, content_width + style.FramePadding.x * 2);
        size.y = std::max(size.y, ImGui::GetFrameHeight());
        const auto* stable_id = std::strstr(label, "###");
        const auto button_id = "###" + std::string(stable_id ? stable_id + 3 : label);
        const bool pressed = ImGui::Button(button_id.c_str(), size);
        if(!ImGui::IsItemVisible())
            return pressed;
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        const float x = minimum.x + (maximum.x - minimum.x - content_width) * 0.5f;
        const float center_y = (minimum.y + maximum.y) * 0.5f;
        const auto color = ImGui::GetColorU32(ImGuiCol_Text);
        ImGui::GetWindowDrawList()->AddText(
            {x + icon_size + style.ItemInnerSpacing.x, center_y - text_size.y * 0.5f}, color, label,
            ImGui::FindRenderedTextEnd(label));
        icons->draw(atlas, icon, {x, center_y - icon_size * 0.5f}, icon_size, color);
        return pressed;
    }
}

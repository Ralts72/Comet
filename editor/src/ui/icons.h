#pragma once

#include "common/result.h"

#include <imgui.h>
#include <array>
#include <filesystem>

namespace CometEditor::Ui {
    enum class Icon { Play, Stop, Pause, Step, Reload, Settings, Input, Count };

    class IconAtlas {
    public:
        Comet::Result<void> load(ImFontAtlas& atlas, const std::filesystem::path& directory);
        void draw(ImFontAtlas& atlas, Icon icon, ImVec2 position, float size, ImU32 color) const;

    private:
        struct Region {
            ImFontAtlasRectId id = ImFontAtlasRectId_Invalid;
            int left = 0, top = 0, right = 0, bottom = 0;
        };
        std::array<Region, static_cast<std::size_t>(Icon::Count)> m_regions;
    };

    bool icon_button(Icon icon, const char* label, ImVec2 size = {});
}

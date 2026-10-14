#include "project/app_export_dialog.h"
#include "ui/widgets.h"
#include <algorithm>
#include <utility>
#include <imgui.h>

namespace CometEditor {
    void AppExportDialog::open(const std::filesystem::path& project,
        const std::filesystem::path& build, const std::filesystem::path& startup,
        std::vector<std::filesystem::path> scenes) {
        if(m_busy)
            return;
        m_project = project;
        m_startup = startup;
        m_build = build.string();
        m_destination = (project.parent_path() / (project.filename().string() + ".app")).string();
        std::ranges::sort(scenes);
        m_scenes.clear();
        for(auto& path : scenes)
            if(path != startup)
                m_scenes.push_back({std::move(path), false});
        m_message.clear();
        m_request.reset();
        m_open_requested = true;
    }

    void AppExportDialog::render(bool scene_modified, bool edit_mode) {
        constexpr auto title = "导出 App###Export App";
        if(std::exchange(m_open_requested, false))
            ImGui::OpenPopup(title);
        ImGui::SetNextWindowSize({650, 0}, ImGuiCond_Appearing);
        if(!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::TextWrapped(
            "导出磁盘上已保存的项目。启动场景自动包含，请勾选游戏运行中还会进入的关卡。");
        ImGui::TextWrapped("启动场景：%s", m_startup.generic_string().c_str());
        ImGui::BeginDisabled(m_busy);
        ImGui::SetNextItemWidth(500);
        Ui::input_text("输出 .app###Destination", m_destination);
        ImGui::SetNextItemWidth(500);
        Ui::input_text("Release 构建目录###Build", m_build);
        ImGui::TextWrapped("首次导出或引擎改动后，先构建 app-release。输出目录必须不存在。");
        if(!m_scenes.empty()) {
            ImGui::BeginChild("Scenes", {0, 130}, ImGuiChildFlags_Borders);
            for(auto& scene : m_scenes)
                ImGui::Checkbox(scene.path.generic_string().c_str(), &scene.selected);
            ImGui::EndChild();
        }
        const bool can_export =
            !scene_modified && edit_mode && !m_destination.empty() && !m_build.empty();
        if(scene_modified)
            ImGui::TextWrapped("当前场景有未保存修改，请关闭此窗口并保存后再导出。");
        if(!edit_mode)
            ImGui::TextUnformatted("请停止 Play 后再导出。");
        ImGui::BeginDisabled(!can_export);
        if(ImGui::Button("导出###Export")) {
            AppExportRequest request{m_project, m_build, m_destination, {}};
            for(const auto& scene : m_scenes)
                if(scene.selected)
                    request.scenes.push_back(scene.path);
            m_request = std::move(request);
            m_busy = true;
            m_message = "正在导出，请稍候…";
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_busy);
        if(ImGui::Button("关闭###Close"))
            ImGui::CloseCurrentPopup();
        ImGui::EndDisabled();
        if(!m_message.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 610);
            ImGui::TextWrapped("%s", m_message.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndPopup();
    }

    std::optional<AppExportRequest> AppExportDialog::take_request() {
        return std::exchange(m_request, std::nullopt);
    }

    void AppExportDialog::complete(const Comet::Result<void>& result) {
        m_busy = false;
        m_message = result ? "导出完成：" + m_destination : "导出失败：" + result.error();
    }
}

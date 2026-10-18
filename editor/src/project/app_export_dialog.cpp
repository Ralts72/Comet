#include "project/app_export_dialog.h"
#include "ui/widgets.h"
#include <algorithm>
#include <utility>
#include <imgui.h>

namespace CometEditor {
    void AppExportDialog::open(const std::filesystem::path& project,
        const std::filesystem::path& startup, std::vector<std::filesystem::path> scenes,
        std::span<const std::filesystem::path> included) {
        if(m_busy)
            return;
        m_project = project;
        m_startup = startup;
#ifdef __APPLE__
        constexpr auto suffix = ".app";
#else
        constexpr auto suffix = "-export";
#endif
        m_destination = (project.parent_path() / (project.filename().string() + suffix)).string();
        std::ranges::sort(scenes);
        m_scenes.clear();
        for(auto& path : scenes)
            if(path != startup) {
                const bool required = std::ranges::find(included, path) != included.end();
                m_scenes.push_back({std::move(path), required, required});
            }
        m_message.clear();
        m_log.clear();
        m_open_log = false;
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
            "导出磁盘上已保存的项目。启动场景和项目必需关卡自动包含，可另选其他关卡。");
        ImGui::TextWrapped("启动场景：%s", m_startup.generic_string().c_str());
        ImGui::BeginDisabled(m_busy);
        ImGui::SetNextItemWidth(500);
        Ui::input_text("输出路径###Destination", m_destination);
        ImGui::TextWrapped("自动构建运行程序并打包资源。首次导出可能较慢，输出目录必须不存在。");
        if(!m_scenes.empty()) {
            ImGui::BeginChild("Scenes", {0, 130}, ImGuiChildFlags_Borders);
            for(auto& scene : m_scenes) {
                ImGui::BeginDisabled(scene.required);
                ImGui::Checkbox(scene.path.generic_string().c_str(), &scene.selected);
                ImGui::EndDisabled();
            }
            ImGui::EndChild();
        }
        const bool can_export = !scene_modified && edit_mode && !m_destination.empty();
        if(scene_modified)
            ImGui::TextWrapped("当前场景有未保存修改，请关闭此窗口并保存后再导出。");
        if(!edit_mode)
            ImGui::TextUnformatted("请停止 Play 后再导出。");
        ImGui::BeginDisabled(!can_export);
        if(ImGui::Button("导出###Export")) {
            AppExportRequest request{m_project, m_destination, {}};
            for(const auto& scene : m_scenes)
                if(scene.selected)
                    request.scenes.push_back(scene.path);
            m_request = std::move(request);
            m_busy = true;
            m_log.clear();
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
        if(!m_log.empty()) {
            if(std::exchange(m_open_log, false))
                ImGui::SetNextItemOpen(true);
            if(ImGui::CollapsingHeader("导出日志###ExportLogHeader")) {
                if(ImGui::Button("复制日志###CopyLog"))
                    ImGui::SetClipboardText(m_log.c_str());
                ImGui::BeginChild("ExportLog", {0, 180}, ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_HorizontalScrollbar);
                const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
                ImGui::TextUnformatted(m_log.c_str());
                if(at_bottom)
                    ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();
            }
        }
        ImGui::EndPopup();
    }

    void AppExportDialog::append_output(std::string_view output) {
        if(output.empty())
            return;
        m_log.append(output);
        if(m_log.size() > 65536)
            m_log.erase(0, m_log.size() - 65536);
        constexpr std::string_view prefix = "[Comet export] ";
        const auto stage = m_log.rfind(prefix);
        if(stage == std::string::npos)
            return;
        const auto start = stage + prefix.size();
        const auto end = m_log.find('\n', start);
        if(end != std::string::npos)
            m_message = m_log.substr(start, end - start);
    }

    std::optional<AppExportRequest> AppExportDialog::take_request() {
        return std::exchange(m_request, std::nullopt);
    }

    void AppExportDialog::complete(const Comet::Result<void>& result) {
        m_busy = false;
        m_message = result ? "导出完成：" + m_destination : "导出失败，请查看日志。";
        if(!result) {
            m_log = result.error();
            m_open_log = true;
        }
    }
}

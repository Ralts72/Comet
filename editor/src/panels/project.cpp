#include "project.h"
#include "selection.h"

#include <imgui.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace CometEditor {
    namespace {
        struct AssetTreeNode {
            std::map<std::string, AssetTreeNode> directories;
            std::vector<const Comet::AssetRecord*> assets;
        };

        AssetTreeNode build_asset_tree(
            const std::vector<Comet::AssetRecord>& assets) {
            AssetTreeNode root;
            for(const Comet::AssetRecord& asset: assets) {
                AssetTreeNode* node = &root;
                for(const auto& component: asset.path.parent_path()) {
                    node = &node->directories[component.string()];
                }
                node->assets.push_back(&asset);
            }
            return root;
        }

        void render_asset_tree(
            const AssetTreeNode& node,
            SelectionService& selection) {
            for(const auto& [name, directory]: node.directories) {
                if(ImGui::TreeNodeEx(
                       name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    render_asset_tree(directory, selection);
                    ImGui::TreePop();
                }
            }

            for(const Comet::AssetRecord* asset: node.assets) {
                const std::string name = asset->path.filename().string();
                if(ImGui::Selectable(
                       name.c_str(), selection.is_selected(asset->handle))) {
                    selection.select_asset(asset->handle);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", Comet::to_string(asset->type).data());
                if(ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "%s\nHandle: %llu",
                        asset->path.generic_string().c_str(),
                        static_cast<unsigned long long>(asset->handle.value()));
                }
            }
        }
    }

    ProjectPanel::ProjectPanel(
        const Comet::AssetDatabase& database,
        Comet::AssetScanReport scan_report,
        RefreshCallback refresh_callback,
        MoveCallback move_callback,
        SelectionService& selection)
        : EditorPanel("Project"),
          m_database(database),
          m_assets(database.get_assets()),
          m_scan_report(std::move(scan_report)),
          m_refresh_callback(std::move(refresh_callback)),
          m_move_callback(std::move(move_callback)),
          m_selection(selection) {}

    void ProjectPanel::render() {
        if(!m_user_visible) return;

        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            ImGui::End();
            return;
        }

        if(ImGui::Button("assets##Button")) {
            m_view_mode = 0;
        }
        ImGui::SameLine();
        if(ImGui::Button("Packages")) {
            m_view_mode = 1;
        }
        ImGui::SameLine();
        if(ImGui::Button("Refresh") && m_refresh_callback) {
            update_scan_report(m_refresh_callback());
        }
        const Comet::AssetRecord* selected_record =
            m_database.find(m_selection.get_selected_asset());
        ImGui::SameLine();
        ImGui::BeginDisabled(!selected_record || !m_move_callback);
        if(ImGui::Button("Move / Rename")) {
            request_asset_move(*selected_record);
        }
        if(ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Move or rename the selected asset");
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        if(m_view_mode == 0) {
            const AssetTreeNode tree = build_asset_tree(m_assets);
            if(ImGui::TreeNodeEx("assets", ImGuiTreeNodeFlags_DefaultOpen)) {
                if(m_assets.empty()) {
                    ImGui::TextDisabled("No indexed assets");
                } else {
                    render_asset_tree(tree, m_selection);
                }
                ImGui::TreePop();
            }
        } else {
            ImGui::TextDisabled("Packages are not available yet");
        }

        if(!m_scan_report.issues.empty()
           && ImGui::CollapsingHeader(
               "Scan Issues", ImGuiTreeNodeFlags_DefaultOpen)) {
            for(const Comet::AssetScanIssue& issue: m_scan_report.issues) {
                ImGui::BulletText(
                    "%s: %s",
                    issue.path.generic_string().c_str(),
                    issue.message.c_str());
            }
        }

        render_asset_move_dialog();
        ImGui::End();
    }

    void ProjectPanel::request_asset_move(
        const Comet::AssetRecord& record) {
        m_moving_asset = record.handle;
        m_move_error.clear();
        m_move_path_buffer.fill('\0');
        const std::string path = record.path.generic_string();
        std::copy_n(
            path.data(),
            std::min(path.size(), m_move_path_buffer.size() - 1),
            m_move_path_buffer.data());
        m_move_dialog_open_requested = true;
    }

    void ProjectPanel::render_asset_move_dialog() {
        constexpr const char* title = "Move / Rename Asset";
        if(m_move_dialog_open_requested) {
            ImGui::OpenPopup(title);
            m_move_dialog_open_requested = false;
        }
        if(!ImGui::BeginPopupModal(
               title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        ImGui::TextUnformatted("Path relative to assets/");
        ImGui::SetNextItemWidth(520.0f);
        const bool submitted = ImGui::InputText(
            "##AssetPath",
            m_move_path_buffer.data(),
            m_move_path_buffer.size(),
            ImGuiInputTextFlags_EnterReturnsTrue);
        if((ImGui::Button("Move", ImVec2(100.0f, 0.0f)) || submitted)
           && m_move_callback) {
            Comet::AssetScanReport report = m_move_callback(
                m_moving_asset,
                std::filesystem::path(m_move_path_buffer.data()));
            const bool moved = report.snapshot_updated;
            if(!moved) {
                m_move_error = report.issues.empty()
                    ? "Asset move could not be committed"
                    : report.issues.front().message;
            }
            update_scan_report(std::move(report));
            if(moved) {
                ImGui::CloseCurrentPopup();
                m_moving_asset = Comet::INVALID_ASSET_HANDLE;
                m_move_error.clear();
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
            m_moving_asset = Comet::INVALID_ASSET_HANDLE;
            m_move_error.clear();
        }

        if(!m_move_error.empty()) {
            ImGui::PushStyleColor(
                ImGuiCol_Text, ImVec4(0.9f, 0.25f, 0.2f, 1.0f));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
            ImGui::TextWrapped("%s", m_move_error.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }

    void ProjectPanel::update_scan_report(
        Comet::AssetScanReport scan_report) {
        m_scan_report = std::move(scan_report);
        m_assets = m_database.get_assets();
        const Comet::AssetHandle selected_asset =
            m_selection.get_selected_asset();
        if(selected_asset && !m_database.find(selected_asset)) {
            m_selection.clear();
        }
    }
}

#include "project.h"

#include <imgui.h>

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
            Comet::AssetHandle& selected_asset) {
            for(const auto& [name, directory]: node.directories) {
                if(ImGui::TreeNodeEx(
                       name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    render_asset_tree(directory, selected_asset);
                    ImGui::TreePop();
                }
            }

            for(const Comet::AssetRecord* asset: node.assets) {
                const std::string name = asset->path.filename().string();
                if(ImGui::Selectable(
                       name.c_str(), selected_asset == asset->handle)) {
                    selected_asset = asset->handle;
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
        RefreshCallback refresh_callback)
        : EditorPanel("Project"),
          m_database(database),
          m_assets(database.get_assets()),
          m_scan_report(std::move(scan_report)),
          m_refresh_callback(std::move(refresh_callback)) {}

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
            m_scan_report = m_refresh_callback();
            m_assets = m_database.get_assets();
            if(!m_database.find(m_selected_asset)) {
                m_selected_asset = Comet::INVALID_ASSET_HANDLE;
            }
        }

        ImGui::Separator();

        if(m_view_mode == 0) {
            const AssetTreeNode tree = build_asset_tree(m_assets);
            if(ImGui::TreeNodeEx("assets", ImGuiTreeNodeFlags_DefaultOpen)) {
                if(m_assets.empty()) {
                    ImGui::TextDisabled("No indexed assets");
                } else {
                    render_asset_tree(tree, m_selected_asset);
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

        ImGui::End();
    }
}

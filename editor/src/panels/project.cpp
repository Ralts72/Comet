#include "project.h"
#include "selection.h"
#include "asset_drag_drop.h"
#include "command_history.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace CometEditor {
    ProjectPanel::AssetTreeNode ProjectPanel::build_asset_tree() const {
        AssetTreeNode root;
        for(Comet::AssetRecord& asset : m_database.get_assets()) {
            AssetTreeNode* node = &root;
            for(const auto& component : asset.path.parent_path()) {
                node = &node->directories[component.string()];
            }
            node->assets.push_back(std::move(asset));
        }
        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator(m_asset_root, error), end;
        while(!error && iterator != end) {
            if(iterator->is_symlink(error)) {
                iterator.disable_recursion_pending();
            } else if(iterator->is_directory(error)) {
                auto* node = &root;
                for(const auto& part : iterator->path().lexically_relative(m_asset_root))
                    node = &node->directories[part.string()];
            }
            iterator.increment(error);
        }
        return root;
    }

    void ProjectPanel::render_asset_tree(
        const AssetTreeNode& node, const std::filesystem::path& path) {
        for(const auto& [name, directory] : node.directories) {
            const auto directory_path = path / name;
            const bool open = ImGui::TreeNodeEx(name.c_str(),
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
            record_drop_target(directory_path);
            accept_asset_drop(directory_path);
            if(open) {
                render_asset_tree(directory, directory_path);
                ImGui::TreePop();
            }
        }

        for(const Comet::AssetRecord& asset : node.assets) {
            const std::string name = asset.path.filename().string();
            ImGui::PushID(std::to_string(asset.handle.value()).c_str());
            if(ImGui::Selectable(name.c_str(), m_selection.is_selected(asset.handle))) {
                m_selection.select_asset(asset.handle);
            }
            record_drop_target(path);
            const bool can_drag = m_move_asset_callback
                                  || ((asset.type == Comet::AssetType::Mesh
                                          || asset.type == Comet::AssetType::Material
                                          || asset.type == Comet::AssetType::Texture)
                                      && m_history.get_scene());
            if(can_drag && ImGui::BeginDragDropSource()) {
                const AssetDragPayload payload{asset.handle,
                    m_database.get_revision(asset.handle), m_history.generation(),
                    asset.type};
                ImGui::SetDragDropPayload(
                    AssetDragPayload::TYPE, &payload, sizeof(payload), ImGuiCond_Once);
                ImGui::TextUnformatted(name.c_str());
                ImGui::EndDragDropSource();
            }
            if(ImGui::BeginPopupContextItem()) {
                if(asset.type == Comet::AssetType::Mesh && ImGui::MenuItem("Reimport"))
                    m_reimport_request = asset.handle;
                if(ImGui::MenuItem("Rename", nullptr, false, !!m_move_asset_callback))
                    request_rename(asset);
                if(ImGui::MenuItem("Refresh", nullptr, false, !!m_refresh_callback))
                    m_refresh_requested = true;
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", Comet::to_string(asset.type).data());
            if(ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", asset.path.generic_string().c_str());
            }
            ImGui::PopID();
        }
    }

    ProjectPanel::ProjectPanel(const Comet::AssetDatabase& database,
        std::filesystem::path asset_root, Comet::AssetScanReport scan_report,
        RefreshCallback refresh_callback, MoveAssetCallback move_asset_callback,
        SelectionService& selection, const CommandHistory& history)
        : EditorPanel("Project"), m_database(database),
          m_asset_root(std::move(asset_root)), m_tree(build_asset_tree()),
          m_scan_report(std::move(scan_report)),
          m_refresh_callback(std::move(refresh_callback)),
          m_move_asset_callback(std::move(move_asset_callback)), m_selection(selection),
          m_history(history) {}

    void ProjectPanel::render() {
        m_drop_targets.clear();
        if(!m_user_visible)
            return;

        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            ImGui::End();
            return;
        }
        const auto& content = ImGui::GetCurrentWindow()->InnerClipRect;
        m_drop_targets.push_back(
            {{content.Min.x, content.Min.y}, {content.Max.x, content.Max.y}, {}});

        const bool root_open = ImGui::TreeNodeEx(
            "assets", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
        record_drop_target({});
        accept_asset_drop({});
        if(root_open) {
            if(m_tree.assets.empty() && m_tree.directories.empty()) {
                ImGui::TextDisabled("No indexed assets");
            } else {
                render_asset_tree(m_tree, {});
            }
            ImGui::TreePop();
        }

        if(!m_scan_report.issues.empty()
            && ImGui::CollapsingHeader("Scan Issues", ImGuiTreeNodeFlags_DefaultOpen)) {
            for(const Comet::AssetScanIssue& issue : m_scan_report.issues) {
                ImGui::BulletText(
                    "%s: %s", issue.path.generic_string().c_str(), issue.message.c_str());
            }
        }

        if(ImGui::BeginPopupContextWindow(
               "Project actions", ImGuiPopupFlags_MouseButtonRight
                                      | ImGuiPopupFlags_NoOpenOverExistingPopup)) {
            if(ImGui::MenuItem("Refresh", nullptr, false, !!m_refresh_callback))
                m_refresh_requested = true;
            ImGui::EndPopup();
        }

        // 移动/刷新回调会替换目录树，必须等所有节点遍历结束再执行。
        if(auto request = std::exchange(m_pending_move, std::nullopt)) {
            if(m_database.is_current(request->handle, request->revision))
                move_asset(request->handle, request->destination);
            else
                m_operation_error = "Asset changed while dragging; please try again";
        }
        if(std::exchange(m_refresh_requested, false) && m_refresh_callback)
            m_refresh_callback();
        render_rename_dialog();
        if(!m_renaming_asset && !m_operation_error.empty()) {
            ImGui::TextWrapped("%s", m_operation_error.c_str());
        }
        ImGui::End();
    }

    std::optional<Comet::AssetHandle> ProjectPanel::take_mesh_reimport_request() {
        return std::exchange(m_reimport_request, std::nullopt);
    }

    void ProjectPanel::record_drop_target(const std::filesystem::path& directory) {
        ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        rect.ClipWith(ImGui::GetCurrentWindow()->ClipRect);
        if(rect.GetWidth() > 0 && rect.GetHeight() > 0)
            m_drop_targets.push_back(
                {{rect.Min.x, rect.Min.y}, {rect.Max.x, rect.Max.y}, directory});
    }

    std::optional<std::filesystem::path> ProjectPanel::file_drop_directory(
        const Comet::Math::Vec2 position) const {
        if(!m_user_visible || m_drop_targets.empty()
            || ImGui::IsPopupOpen(
                nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
            return std::nullopt;
        ImGuiWindow* hovered = nullptr;
        ImGuiWindow* under_moving = nullptr;
        ImGui::FindHoveredWindowEx(
            {position.x, position.y}, false, &hovered, &under_moving);
        if(hovered != ImGui::FindWindowByName(m_name.c_str()))
            return std::nullopt;
        for(auto it = m_drop_targets.rbegin(); it != m_drop_targets.rend(); ++it) {
            if(position.x >= it->minimum.x && position.y >= it->minimum.y
                && position.x < it->maximum.x && position.y < it->maximum.y)
                return it->directory;
        }
        return std::nullopt;
    }

    void ProjectPanel::accept_asset_drop(const std::filesystem::path& directory) {
        if(!m_move_asset_callback || !ImGui::BeginDragDropTarget())
            return;
        if(const auto* payload = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE);
            payload && payload->DataSize == sizeof(AssetDragPayload)) {
            const auto& source = *static_cast<const AssetDragPayload*>(payload->Data);
            const auto* record = m_database.find(source.handle);
            if(record) {
                const auto destination = directory / record->path.filename();
                if(destination != record->path)
                    m_pending_move =
                        MoveRequest{source.handle, source.revision, destination};
            }
        }
        ImGui::EndDragDropTarget();
    }

    bool ProjectPanel::move_asset(
        const Comet::AssetHandle handle, const std::filesystem::path& destination) {
        m_operation_error.clear();
        if(!m_move_asset_callback)
            return false;
        const auto* record = m_database.find(handle);
        if(record && record->path == destination)
            return true;
        const auto report = m_move_asset_callback(handle, destination);
        if(report.snapshot_updated)
            return true;
        m_operation_error = "Asset operation could not be committed";
        if(!report.issues.empty())
            m_operation_error = report.issues.front().message;
        return false;
    }

    void ProjectPanel::request_rename(const Comet::AssetRecord& record) {
        m_renaming_asset = record.handle;
        m_operation_error.clear();
        m_name_buffer.fill('\0');
        const auto name = record.path.stem().string();
        std::copy_n(name.data(), std::min(name.size(), m_name_buffer.size() - 1),
            m_name_buffer.data());
        m_rename_requested = true;
    }

    void ProjectPanel::render_rename_dialog() {
        constexpr const char* title = "Rename Asset";
        const bool opening = std::exchange(m_rename_requested, false);
        if(opening)
            ImGui::OpenPopup(title);
        if(!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        const auto* record = m_database.find(m_renaming_asset);
        if(!record)
            ImGui::TextDisabled("Asset is no longer available");
        ImGui::BeginDisabled(!record || !m_move_asset_callback);
        if(opening)
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(360.0f);
        const bool submitted =
            ImGui::InputText("Name", m_name_buffer.data(), m_name_buffer.size(),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if(record) {
            ImGui::SameLine();
            ImGui::TextUnformatted(record->path.extension().string().c_str());
        }
        if((ImGui::Button("Rename", ImVec2(100.0f, 0.0f)) || submitted) && record
            && m_move_asset_callback) {
            const std::string name(m_name_buffer.data());
            if(name.empty() || name == "." || name == ".."
                || name.find_first_of("/\\:") != std::string::npos) {
                m_operation_error = "Enter a file name, not a path";
            } else {
                const auto destination = record->path.parent_path()
                                         / (name + record->path.extension().string());
                if(move_asset(m_renaming_asset, destination)) {
                    ImGui::CloseCurrentPopup();
                    m_renaming_asset = Comet::INVALID_ASSET_HANDLE;
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
            m_renaming_asset = Comet::INVALID_ASSET_HANDLE;
            m_operation_error.clear();
        }
        if(!m_operation_error.empty())
            ImGui::TextWrapped("%s", m_operation_error.c_str());
        ImGui::EndPopup();
    }

    void ProjectPanel::update_scan_report(Comet::AssetScanReport scan_report) {
        m_scan_report = std::move(scan_report);
        if(m_scan_report.snapshot_updated)
            m_tree = build_asset_tree();
        const Comet::AssetHandle selected_asset = m_selection.get_selected_asset();
        if(selected_asset && !m_database.find(selected_asset)) {
            m_selection.clear();
        }
    }
}

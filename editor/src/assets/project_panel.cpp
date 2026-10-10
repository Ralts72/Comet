#include "assets/project_panel.h"
#include "scene/selection.h"
#include "assets/asset_reference.h"
#include "scene/command_history.h"
#include "ui/widgets.h"
#include "asset/script.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CometEditor {
    namespace {
        constexpr const char* MODULE_DRAG_TYPE = "COMET_PROJECT_LUA_MODULE";

        bool contains_search(const std::string_view text, const std::string_view query) {
            return std::search(text.begin(), text.end(), query.begin(), query.end(),
                       [](const unsigned char left, const unsigned char right) {
                           return std::tolower(left) == std::tolower(right);
                       })
                   != text.end();
        }

        bool valid_asset_name(const std::string_view name) {
            return !name.empty() && name != "." && name != ".."
                   && name.find_first_of("/\\:") == std::string_view::npos;
        }

        bool input_asset_name(
            std::string& name, const ImGuiInputTextFlags extra_flags = ImGuiInputTextFlags_None) {
            return Ui::input_text(
                "名称###Name", name, ImGuiInputTextFlags_EnterReturnsTrue | extra_flags);
        }

        void render_module_reference(
            const char* label, const std::filesystem::path& path, const float width) {
            const auto name = Comet::Script::module_name(path);
            std::string reference = "无效的模块引用";
            if(name)
                reference = "require(\"" + name.value() + "\")";
            ImGui::SetNextItemWidth(width);
            Ui::input_text(label, reference, ImGuiInputTextFlags_ReadOnly);
        }
    }

    ProjectPanel::AssetTreeNode ProjectPanel::build_asset_tree() const {
        AssetTreeNode root;
        for(Comet::AssetRecord& asset : m_database.get_assets()) {
            AssetTreeNode* node = &root;
            for(const auto& component : asset.path.parent_path()) {
                node = &node->directories[component.string()];
            }
            const std::string filename = asset.path.filename().string();
            node->files.emplace(filename, std::move(asset));
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
            } else if(iterator->is_regular_file(error)) {
                const auto& filename = iterator->path().filename();
                if(filename.extension() != ".meta" && filename != ".DS_Store"
                    && !filename.string().starts_with(".comet-tmp-")) {
                    auto* node = &root;
                    const auto relative = iterator->path().lexically_relative(m_asset_root);
                    for(const auto& part : relative.parent_path())
                        node = &node->directories[part.string()];
                    node->files.try_emplace(filename.string(), std::nullopt);
                }
            }
            iterator.increment(error);
        }
        return root;
    }

    ProjectPanel::AssetTreeNode ProjectPanel::filter_asset_tree(
        const AssetTreeNode& node, const bool include_all) const {
        if(include_all)
            return node;
        AssetTreeNode filtered;
        const std::string_view query(m_search.data());
        for(const auto& [name, directory] : node.directories) {
            const bool directory_matches = contains_search(name, query);
            auto child = filter_asset_tree(directory, directory_matches);
            if(directory_matches || !child.files.empty() || !child.directories.empty())
                filtered.directories.emplace(name, std::move(child));
        }
        for(const auto& [name, asset] : node.files)
            if(contains_search(name, query))
                filtered.files.emplace(name, asset);
        return filtered;
    }

    void ProjectPanel::rebuild_search_tree() {
        if(m_search.front() == '\0')
            m_filtered_tree.reset();
        else
            m_filtered_tree = filter_asset_tree(m_tree, false);
    }

    void ProjectPanel::render_asset_tree(
        const AssetTreeNode& node, const std::filesystem::path& path) {
        for(const auto& [name, directory] : node.directories) {
            const auto directory_path = path / name;
            if(m_filtered_tree)
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            const bool open = ImGui::TreeNodeEx(
                name.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
            record_drop_target(directory_path);
            accept_internal_drop(directory_path);
            render_directory_menu(directory_path);
            if(open) {
                render_asset_tree(directory, directory_path);
                ImGui::TreePop();
            }
        }

        for(const auto& [name, indexed_asset] : node.files) {
            if(!indexed_asset) {
                const auto source = path / name;
                ImGui::PushID(source.generic_string().c_str());
                const bool module = name.ends_with(".module.lua");
                const bool editable = AssetSourceOperations::can_open_source(m_database, source);
                if(module || editable)
                    ImGui::Selectable(name.c_str(), false);
                else
                    ImGui::TextUnformatted(name.c_str());
                record_drop_target(path);
                if(module && ImGui::BeginDragDropSource()) {
                    const auto absolute = (m_asset_root / source).generic_string();
                    ImGui::SetDragDropPayload(
                        MODULE_DRAG_TYPE, absolute.data(), absolute.size(), ImGuiCond_Once);
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::TextUnformatted("require 引用不会自动修改。");
                    ImGui::EndDragDropSource();
                }
                if((module || editable)
                    && ImGui::BeginPopupContextItem(module ? "Module actions" : "Source actions")) {
                    if(ImGui::MenuItem("打开源码###Open Source"))
                        m_pending_open_source = source;
                    if(module && ImGui::MenuItem("重命名###Rename"))
                        request_rename(source);
                    if(module && ImGui::MenuItem("删除###Delete"))
                        request_delete(source);
                    ImGui::EndPopup();
                }
                if(ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", source.generic_string().c_str());
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", "文件");
                ImGui::PopID();
                continue;
            }
            const Comet::AssetRecord& asset = *indexed_asset;
            ImGui::PushID(std::to_string(asset.handle.value()).c_str());
            if(ImGui::Selectable(name.c_str(), m_selection.is_selected(asset.handle)))
                m_selection.select_asset(asset.handle);
            record_drop_target(path);
            if(ImGui::BeginDragDropSource()) {
                const AssetDragPayload payload{asset.handle, m_database.get_revision(asset.handle),
                    m_history.generation(), asset.type};
                ImGui::SetDragDropPayload(
                    AssetDragPayload::TYPE, &payload, sizeof(payload), ImGuiCond_Once);
                ImGui::TextUnformatted(name.c_str());
                ImGui::EndDragDropSource();
            }
            if(ImGui::BeginPopupContextItem()) {
                if(AssetSourceOperations::can_open_source(m_database, asset.path)
                    && ImGui::MenuItem("打开源码###Open Source"))
                    m_pending_open_source = asset.path;
                if(asset.type == Comet::AssetType::Mesh && ImGui::MenuItem("重新导入###Reimport"))
                    m_reimport_request = asset.handle;
                if(ImGui::MenuItem("重命名###Rename"))
                    request_rename(asset);
                if(ImGui::MenuItem("删除###Delete"))
                    request_delete(asset);
                if(ImGui::MenuItem("刷新###Refresh"))
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
        SelectionService& selection, const CommandHistory& history)
        : EditorPanel("项目###Project"), m_database(database), m_asset_root(std::move(asset_root)),
          m_tree(build_asset_tree()), m_scan_report(std::move(scan_report)), m_selection(selection),
          m_history(history) {
        const auto builtins = Comet::MaterialLayout::builtins();
        m_material_layouts.assign(builtins.begin(), builtins.end());
    }

    void ProjectPanel::render() {
        m_drop_targets.clear();
        if(!m_user_visible)
            return;

        if(!ImGui::Begin(window_label().c_str(), &m_user_visible)) {
            ImGui::End();
            return;
        }
        const auto& content = ImGui::GetCurrentWindow()->InnerClipRect;
        m_drop_targets.push_back(
            {{content.Min.x, content.Min.y}, {content.Max.x, content.Max.y}, {}});

        ImGui::SetNextItemWidth(-1.0f);
        if(ImGui::InputTextWithHint(
               "##asset_search", "搜索文件...", m_search.data(), m_search.size()))
            rebuild_search_tree();

        const auto& visible_tree = m_filtered_tree ? *m_filtered_tree : m_tree;
        if(m_filtered_tree)
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        const bool root_open = ImGui::TreeNodeEx(
            "assets", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
        record_drop_target({});
        accept_internal_drop({});
        render_directory_menu({});
        if(root_open) {
            if(visible_tree.files.empty() && visible_tree.directories.empty()) {
                ImGui::TextDisabled("%s", m_filtered_tree ? "没有匹配的文件" : "暂无文件");
            } else {
                render_asset_tree(visible_tree, {});
            }
            ImGui::TreePop();
        }

        if(!m_scan_report.issues.empty()
            && ImGui::CollapsingHeader("扫描问题###Scan Issues", ImGuiTreeNodeFlags_DefaultOpen)) {
            for(const Comet::AssetScanIssue& issue : m_scan_report.issues) {
                ImGui::BulletText(
                    "%s: %s", issue.path.generic_string().c_str(), issue.message.c_str());
            }
        }

        if(ImGui::BeginPopupContextWindow("Project actions",
               ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverExistingPopup)) {
            if(ImGui::MenuItem(
                   "新建材质...###New Material...", nullptr, false, !m_material_layouts.empty()))
                request_create_material({});
            if(ImGui::MenuItem("新建脚本...###New Script..."))
                request_create_script({});
            if(ImGui::MenuItem("新建 Lua 模块...###New Lua Module..."))
                request_create_script({}, AssetSourceOperations::ScriptKind::Module);
            if(ImGui::MenuItem("刷新###Refresh"))
                m_refresh_requested = true;
            ImGui::EndPopup();
        }

        render_rename_dialog();
        render_delete_dialog();
        render_create_material_dialog();
        render_create_script_dialog();
        if(std::holds_alternative<std::monostate>(m_rename_target) && !m_operation_error.empty()) {
            ImGui::TextWrapped("%s", m_operation_error.c_str());
        }
        ImGui::End();
    }

    void ProjectPanel::set_material_layouts(
        std::vector<std::shared_ptr<const Comet::MaterialLayout>> layouts) {
        std::erase(layouts, nullptr);
        std::ranges::sort(layouts, {}, [](const auto& layout) { return layout->get_name(); });
        m_material_layouts = std::move(layouts);
    }

    void ProjectPanel::render_directory_menu(const std::filesystem::path& directory) {
        if(ImGui::BeginPopupContextItem()) {
            if(ImGui::MenuItem(
                   "新建材质...###New Material...", nullptr, false, !m_material_layouts.empty()))
                request_create_material(directory);
            if(ImGui::MenuItem("新建脚本...###New Script..."))
                request_create_script(directory);
            if(ImGui::MenuItem("新建 Lua 模块...###New Lua Module..."))
                request_create_script(directory, AssetSourceOperations::ScriptKind::Module);
            ImGui::EndPopup();
        }
    }

    void ProjectPanel::request_create_material(const std::filesystem::path& directory) {
        m_create_directory = directory;
        m_create_name.clear();
        m_create_template.clear();
        if(!m_material_layouts.empty())
            m_create_template = m_material_layouts.front()->get_name();
        m_operation_error.clear();
        m_close_create = false;
        m_create_requested = true;
    }

    void ProjectPanel::request_create_script(
        const std::filesystem::path& directory, const AssetSourceOperations::ScriptKind kind) {
        m_create_directory = directory;
        m_create_name.clear();
        m_create_script_kind = kind;
        m_operation_error.clear();
        m_close_create_script = false;
        m_create_script_requested = true;
    }

    void ProjectPanel::render_create_material_dialog() {
        constexpr const char* title = "新建材质###New Material";
        const bool opening = std::exchange(m_create_requested, false);
        if(opening)
            ImGui::OpenPopup(title);
        if(!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        if(std::exchange(m_close_create, false)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::Text("目录：assets/%s", m_create_directory.generic_string().c_str());
        if(opening)
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(320.0f);
        const bool submitted = input_asset_name(m_create_name);
        ImGui::SameLine();
        ImGui::TextUnformatted(".mat");
        ImGui::SetNextItemWidth(320.0f);
        if(ImGui::BeginCombo("渲染模板###Render Template", m_create_template.c_str())) {
            for(const auto& layout : m_material_layouts) {
                const bool selected = layout->get_name() == m_create_template;
                if(ImGui::Selectable(layout->get_name().c_str(), selected))
                    m_create_template = layout->get_name();
                if(selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if(ImGui::Button("创建###Create") || submitted) {
            const auto layout = std::ranges::find_if(m_material_layouts,
                [&](const auto& item) { return item->get_name() == m_create_template; });
            if(!valid_asset_name(m_create_name))
                m_operation_error = "Enter a file name, not a path";
            else if(layout == m_material_layouts.end())
                m_operation_error = "Selected template is no longer available";
            else if(std::ranges::any_of((*layout)->get_textures(),
                        [](const auto& property) { return !property.optional; }))
                m_operation_error = "This template requires textures before it can be created";
            else {
                auto filename = m_create_name;
                if(!filename.ends_with(".mat"))
                    filename += ".mat";
                m_pending_create = CreateMaterialRequest{
                    m_create_directory / filename, make_material_data(**layout)};
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("取消###Cancel")) {
            ImGui::CloseCurrentPopup();
            m_operation_error.clear();
        }
        if(!m_operation_error.empty())
            ImGui::TextWrapped("%s", m_operation_error.c_str());
        ImGui::EndPopup();
    }

    std::optional<ProjectPanel::CreateMaterialRequest> ProjectPanel::
        take_create_material_request() {
        return std::exchange(m_pending_create, std::nullopt);
    }

    void ProjectPanel::render_create_script_dialog() {
        const bool module = m_create_script_kind == AssetSourceOperations::ScriptKind::Module;
        const char* title = module ? "新建 Lua 模块###New Lua Module" : "新建脚本###New Script";
        const std::string_view suffix = module ? ".module.lua" : ".lua";
        const bool opening = std::exchange(m_create_script_requested, false);
        if(opening)
            ImGui::OpenPopup(title);
        if(!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        if(std::exchange(m_close_create_script, false)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::Text("目录：assets/%s", m_create_directory.generic_string().c_str());
        if(opening)
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(320.0f);
        const bool submitted = input_asset_name(m_create_name);
        ImGui::SameLine();
        ImGui::TextUnformatted(suffix.data());
        auto name = m_create_name;
        if(!name.ends_with(suffix))
            name += suffix;
        const auto destination = m_create_directory / name;
        const auto module_name = Comet::Script::module_name(destination);
        if(module) {
            ImGui::TextWrapped("%s", "Lua 模块仅供脚本复用，不能直接挂载到实体。");
            if(module_name)
                render_module_reference("模块引用###Module reference", destination, 320.0f);
        }
        if(ImGui::Button("创建###Create") || submitted) {
            if(!valid_asset_name(m_create_name))
                m_operation_error = "Enter a file name, not a path";
            else if(module && !module_name)
                m_operation_error = module_name.error();
            else {
                m_pending_script_create = CreateScriptRequest{destination, m_create_script_kind};
                m_operation_error.clear();
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("取消###Cancel")) {
            ImGui::CloseCurrentPopup();
            m_operation_error.clear();
        }
        if(!m_operation_error.empty())
            ImGui::TextWrapped("%s", m_operation_error.c_str());
        ImGui::EndPopup();
    }

    std::optional<ProjectPanel::CreateScriptRequest> ProjectPanel::take_create_script_request() {
        return std::exchange(m_pending_script_create, std::nullopt);
    }

    std::optional<std::filesystem::path> ProjectPanel::take_open_source_request() {
        return std::exchange(m_pending_open_source, std::nullopt);
    }

    void ProjectPanel::complete_open_source(const Comet::Result<void>& result) {
        m_operation_error.clear();
        if(!result)
            m_operation_error = result.error();
    }

    void ProjectPanel::complete_create_material(
        const CreateMaterialRequest& request, const Comet::AssetScanReport& report) {
        complete_create_asset(request.destination, report, false);
    }

    void ProjectPanel::complete_create_script(
        const CreateScriptRequest& request, const Comet::AssetScanReport& report) {
        complete_create_asset(request.destination, report, true);
    }

    void ProjectPanel::complete_create_asset(const std::filesystem::path& destination,
        const Comet::AssetScanReport& report, const bool script) {
        m_operation_error.clear();
        const bool committed = report.snapshot_updated && report.succeeded();
        if(!committed) {
            m_operation_error = "Asset could not be created";
            if(!report.issues.empty())
                m_operation_error = report.issues.front().message;
        }
        if(committed) {
            if(const auto* record = m_database.find(destination))
                m_selection.select_asset(record->handle);
            if(script)
                m_close_create_script = true;
            else
                m_close_create = true;
        }
    }

    std::optional<Comet::AssetHandle> ProjectPanel::take_mesh_reimport_request() {
        return std::exchange(m_reimport_request, std::nullopt);
    }

    bool ProjectPanel::take_refresh_request() {
        return std::exchange(m_refresh_requested, false);
    }

    std::optional<ProjectPanel::MoveRequest> ProjectPanel::take_move_request() {
        auto request = std::exchange(m_pending_move, std::nullopt);
        if(request && !m_database.is_current(request->handle, request->revision)) {
            m_operation_error = "Asset changed while editing; please try again";
            return std::nullopt;
        }
        return request;
    }

    std::optional<ProjectPanel::ModuleMoveRequest> ProjectPanel::take_move_module_request() {
        return std::exchange(m_pending_module_move, std::nullopt);
    }

    std::optional<ProjectPanel::DeleteRequest> ProjectPanel::take_delete_request() {
        auto request = std::exchange(m_pending_delete, std::nullopt);
        if(request && !m_database.is_current(request->handle, request->revision)) {
            m_operation_error = "Asset changed while editing; please try again";
            return std::nullopt;
        }
        return request;
    }

    std::optional<ProjectPanel::ModuleDeleteRequest> ProjectPanel::take_delete_module_request() {
        return std::exchange(m_pending_module_delete, std::nullopt);
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
        ImGui::FindHoveredWindowEx({position.x, position.y}, false, &hovered, &under_moving);
        if(hovered != ImGui::FindWindowByName(m_name.c_str()))
            return std::nullopt;
        for(auto it = m_drop_targets.rbegin(); it != m_drop_targets.rend(); ++it) {
            if(position.x >= it->minimum.x && position.y >= it->minimum.y
                && position.x < it->maximum.x && position.y < it->maximum.y)
                return it->directory;
        }
        return std::nullopt;
    }

    void ProjectPanel::accept_internal_drop(const std::filesystem::path& directory) {
        if(!ImGui::BeginDragDropTarget())
            return;
        if(const auto payload =
                read_asset_drag_payload(ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE))) {
            const auto& source = *payload;
            const auto* record = m_database.find(source.handle);
            if(record) {
                const auto destination = directory / record->path.filename();
                if(destination != record->path)
                    m_pending_move = MoveRequest{source.handle, source.revision, destination};
            }
        }
        if(const auto* payload = ImGui::AcceptDragDropPayload(MODULE_DRAG_TYPE)) {
            const std::string_view path(static_cast<const char*>(payload->Data), payload->DataSize);
            if(!path.empty() && path.find('\0') == std::string_view::npos) {
                const auto source = std::filesystem::path(path).lexically_relative(m_asset_root);
                const auto destination = directory / source.filename();
                if(source != destination && Comet::Script::module_name(source))
                    m_pending_module_move = ModuleMoveRequest{source, destination};
            }
        }
        ImGui::EndDragDropTarget();
    }

    void ProjectPanel::complete_move(
        const MoveRequest& request, const Comet::AssetScanReport& report) {
        m_operation_error.clear();
        const bool committed = report.snapshot_updated && report.succeeded();
        if(!committed) {
            m_operation_error = "Asset operation could not be committed";
            if(!report.issues.empty())
                m_operation_error = report.issues.front().message;
        }
        const auto* target = std::get_if<Comet::AssetHandle>(&m_rename_target);
        if(committed && target && *target == request.handle)
            m_close_rename = true;
    }

    void ProjectPanel::complete_move_module(
        const ModuleMoveRequest& request, const Comet::AssetScanReport& report) {
        m_operation_error.clear();
        const bool committed = report.snapshot_updated && report.succeeded();
        if(!committed) {
            m_operation_error = "无法移动或重命名模块";
            if(!report.issues.empty())
                m_operation_error = report.issues.front().message;
        }
        const auto* target = std::get_if<std::filesystem::path>(&m_rename_target);
        if(committed && target && *target == request.source)
            m_close_rename = true;
    }

    void ProjectPanel::complete_delete(
        const DeleteRequest& request, const Comet::AssetScanReport& report) {
        m_operation_error.clear();
        const bool committed = report.snapshot_updated && report.succeeded();
        if(!committed) {
            m_operation_error = "Asset could not be deleted";
            if(!report.issues.empty())
                m_operation_error = report.issues.front().message;
        }
        const auto* target = std::get_if<Comet::AssetHandle>(&m_delete_target);
        if(committed && target && *target == request.handle)
            m_close_delete = true;
    }

    void ProjectPanel::complete_delete_module(
        const ModuleDeleteRequest& request, const Comet::AssetScanReport& report) {
        m_operation_error.clear();
        const bool committed = report.snapshot_updated && report.succeeded();
        if(!committed) {
            m_operation_error = "无法删除模块";
            if(!report.issues.empty())
                m_operation_error = report.issues.front().message;
        }
        const auto* target = std::get_if<std::filesystem::path>(&m_delete_target);
        if(committed && target && *target == request.source)
            m_close_delete = true;
    }

    void ProjectPanel::request_delete(const Comet::AssetRecord& record) {
        m_delete_target = record.handle;
        m_delete_requested = true;
        m_close_delete = false;
        m_operation_error.clear();
    }

    void ProjectPanel::request_delete(const std::filesystem::path& module) {
        m_delete_target = module;
        m_delete_requested = true;
        m_close_delete = false;
        m_operation_error.clear();
    }

    void ProjectPanel::request_delete_selection() {
        if(const auto* record = m_database.find(m_selection.get_selected_asset()))
            request_delete(*record);
    }

    void ProjectPanel::render_delete_dialog() {
        const auto* module = std::get_if<std::filesystem::path>(&m_delete_target);
        const char* title = module ? "删除 Lua 模块" : "删除资产";
        const auto label = std::string(title) + "###Delete Asset";
        if(std::exchange(m_delete_requested, false))
            ImGui::OpenPopup("Delete Asset");
        if(!ImGui::BeginPopupModal(label.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        if(std::exchange(m_close_delete, false)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            m_delete_target = std::monostate{};
            return;
        }
        const auto* asset = std::get_if<Comet::AssetHandle>(&m_delete_target);
        const auto* record = asset ? m_database.find(*asset) : nullptr;
        if(module) {
            ImGui::TextWrapped(
                "将源码文件 assets/%s 移到系统回收站？", module->generic_string().c_str());
            render_module_reference("模块引用###Module reference", *module, 360.0f);
            ImGui::TextWrapped("%s", "require 引用不会自动修改。");
        } else if(record)
            ImGui::TextWrapped(
                "将 assets/%s 及其元数据移到系统回收站？", record->path.generic_string().c_str());
        else
            ImGui::TextDisabled("%s", "资产已不可用");
        if(!module)
            ImGui::TextDisabled("%s", "场景中的引用不会自动清除。");
        ImGui::BeginDisabled(!record && !module);
        if(ImGui::Button("移到系统回收站###Move to Trash")) {
            if(module)
                m_pending_module_delete = ModuleDeleteRequest{*module};
            else if(record)
                m_pending_delete =
                    DeleteRequest{record->handle, m_database.get_revision(record->handle)};
            m_operation_error.clear();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::Button("取消###Cancel")) {
            ImGui::CloseCurrentPopup();
            m_delete_target = std::monostate{};
            m_operation_error.clear();
        }
        if(!m_operation_error.empty())
            ImGui::TextWrapped("%s", m_operation_error.c_str());
        ImGui::EndPopup();
    }

    void ProjectPanel::request_rename(const Comet::AssetRecord& record) {
        m_rename_target = record.handle;
        m_close_rename = false;
        m_operation_error.clear();
        m_rename_name = record.path.stem().string();
        m_rename_requested = true;
    }

    void ProjectPanel::request_rename(const std::filesystem::path& module) {
        constexpr std::string_view suffix = ".module.lua";
        m_rename_target = module;
        m_close_rename = false;
        m_operation_error.clear();
        m_rename_name = module.filename().string();
        m_rename_name.resize(m_rename_name.size() - suffix.size());
        m_rename_requested = true;
    }

    void ProjectPanel::render_rename_dialog() {
        constexpr const char* title = "重命名资产###Rename Asset";
        const bool opening = std::exchange(m_rename_requested, false);
        if(opening)
            ImGui::OpenPopup(title);
        if(!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        if(std::exchange(m_close_rename, false)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            m_rename_target = std::monostate{};
            return;
        }

        const auto* asset = std::get_if<Comet::AssetHandle>(&m_rename_target);
        const auto* module = std::get_if<std::filesystem::path>(&m_rename_target);
        const auto* record = asset ? m_database.find(*asset) : nullptr;
        const bool available = record || module;
        std::filesystem::path source;
        if(module)
            source = *module;
        else if(record)
            source = record->path;
        const std::string suffix = module ? ".module.lua" : source.extension().string();
        if(!available)
            ImGui::TextDisabled("%s", "资产已不可用");
        ImGui::BeginDisabled(!available);
        if(opening)
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(360.0f);
        const bool submitted = input_asset_name(m_rename_name, ImGuiInputTextFlags_AutoSelectAll);
        if(available) {
            ImGui::SameLine();
            ImGui::TextUnformatted(suffix.c_str());
        }
        auto filename = m_rename_name;
        if(!module || !filename.ends_with(suffix))
            filename += suffix;
        const auto destination = source.parent_path() / filename;
        if(module) {
            render_module_reference("Current module reference", source, 360.0f);
            render_module_reference("New module reference", destination, 360.0f);
            ImGui::TextWrapped("%s", "源代码不会自动修改，请手工更新 require 引用。");
        }
        if((ImGui::Button("重命名###Rename", ImVec2(100.0f, 0.0f)) || submitted) && available) {
            if(!valid_asset_name(m_rename_name)) {
                m_operation_error = "Enter a file name, not a path";
            } else if(module) {
                const auto name = Comet::Script::module_name(destination);
                if(!name)
                    m_operation_error = name.error();
                else if(source == destination) {
                    ImGui::CloseCurrentPopup();
                    m_rename_target = std::monostate{};
                    m_operation_error.clear();
                } else {
                    m_pending_module_move = ModuleMoveRequest{source, destination};
                    m_operation_error.clear();
                }
            } else {
                if(source == destination) {
                    ImGui::CloseCurrentPopup();
                    m_rename_target = std::monostate{};
                    m_operation_error.clear();
                } else
                    m_pending_move = MoveRequest{
                        record->handle, m_database.get_revision(record->handle), destination};
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::Button("取消###Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
            m_rename_target = std::monostate{};
            m_operation_error.clear();
        }
        if(!m_operation_error.empty())
            ImGui::TextWrapped("%s", m_operation_error.c_str());
        ImGui::EndPopup();
    }

    void ProjectPanel::update_scan_report(Comet::AssetScanReport scan_report) {
        m_scan_report = std::move(scan_report);
        if(m_scan_report.snapshot_updated) {
            m_tree = build_asset_tree();
            rebuild_search_tree();
        }
        const Comet::AssetHandle selected_asset = m_selection.get_selected_asset();
        if(selected_asset && !m_database.find(selected_asset)) {
            m_selection.clear();
        }
    }
}

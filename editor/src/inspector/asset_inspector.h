#pragma once

#include "asset/data/game_data.h"

#include "asset/database.h"
#include "assets/asset_edit.h"
#include "assets/material_editing.h"
#include "animation/animation_controller_editor.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Comet {
    class MaterialProgramValidator;
    class ShaderProgramArtifact;
}

namespace CometEditor {
    // 拥有资产编辑草稿；文件与 GPU 操作仍由宿主消费请求后执行。
    class RetargetPreview;
    class AssetInspector {
    public:
        AssetInspector(
            const Comet::AssetDatabase& database, const Comet::MaterialProgramValidator& programs);
        void select(Comet::AssetHandle handle);
        void set_retarget_preview(RetargetPreview* preview) { m_retarget_preview = preview; }
        void complete_asset_read(
            const Comet::AssetTarget& request, Comet::Result<Comet::GameData> result);
        void complete_asset_read(
            const Comet::AssetTarget& request, Comet::Result<Comet::TerrainData> result);
        void complete_asset_read(
            const Comet::AssetTarget& request, Comet::Result<Comet::AnimationLibrary> result);
        void render(std::uint64_t generation, bool allow_drop);
        void set_material_layouts(
            std::vector<std::shared_ptr<const Comet::MaterialLayout>> layouts);
        [[nodiscard]] std::optional<AssetEdit> take_asset_edit();
        [[nodiscard]] std::optional<Comet::AssetTarget> take_asset_read();
        void complete_asset_read(
            const Comet::AssetTarget& request, Comet::Result<Comet::MaterialData> result);
        void complete_asset_edit(const AssetEdit& edit, bool succeeded, std::string error = {});
        void finish_material_edit(bool cancel = false);
        void complete_asset_read(
            const Comet::AssetTarget& request, Comet::Result<Comet::AnimationController> result);
        [[nodiscard]] std::optional<Comet::AssetHandle> take_selection_request();
        [[nodiscard]] bool finish_animation_edit();
        void render_controller_window(AnimationPreview* preview);
        [[nodiscard]] std::optional<Comet::AssetTarget> take_library_read();
        void open_controller(Comet::AssetHandle handle);
        [[nodiscard]] AnimationControllerEditor& controller_editor() { return m_controller_editor; }
        [[nodiscard]] bool controller_active() const { return m_controller_data.has_value(); }
        bool undo_controller(bool redo = false);
        [[nodiscard]] Comet::AssetHandle controller_handle() const {
            return m_loaded_target.handle;
        }
        [[nodiscard]] bool animation_selection_pending() const {
            return animation_dirty() && m_next_asset.has_value();
        }

    private:
        void render_animation_controller();
        void render_animation_library();
        void render_unsaved_animation();
        [[nodiscard]] bool animation_dirty() const;
        void render_data();
        void render_terrain();
        void render_texture(const Comet::AssetRecord& record);
        void render_audio(const Comet::AssetRecord& record);
        void render_material(
            const Comet::AssetRecord& record, std::uint64_t generation, bool allow_drop);
        void confirm_material_template();
        void load_asset(const Comet::AssetRecord& record);
        void reimport_texture(const Comet::AssetRecord& record,
            const Comet::TextureImportSettings& previous_settings);
        void update_material(const Comet::AssetRecord& record,
            const Comet::MaterialData& previous_data,
            AssetEdit::Action action = AssetEdit::Action::Apply);
        [[nodiscard]] std::string validate_material() const;
        [[nodiscard]] std::shared_ptr<const Comet::MaterialLayout> material_layout() const;
        [[nodiscard]] std::shared_ptr<const Comet::MaterialLayout> layout_for(
            Comet::AssetHandle program, const std::string& template_name) const;

        RetargetPreview* m_retarget_preview = nullptr;
        const Comet::AssetDatabase& m_asset_database;
        const Comet::MaterialProgramValidator& m_material_parameters;
        Comet::AssetHandle m_selected_asset;
        Comet::AssetTarget m_loaded_target;
        Comet::AssetTarget m_library_target;
        std::optional<Comet::TextureImportSettings> m_texture_import_settings;
        std::optional<Comet::AudioImportSettings> m_audio_import_settings;
        std::optional<Comet::MaterialData> m_material_data;
        std::optional<Comet::GameData> m_data;
        std::optional<Comet::TerrainData> m_terrain_data;
        std::optional<Comet::TerrainData> m_terrain_saved;
        std::optional<Comet::AnimationLibrary> m_library_data;
        std::optional<Comet::AnimationLibrary> m_library_saved;
        std::optional<Comet::MaterialData> m_material_before;
        std::optional<Comet::AnimationController> m_controller_data;
        std::optional<Comet::AnimationController> m_controller_saved;
        AnimationControllerEditor m_controller_editor;
        std::optional<Comet::AssetHandle> m_selection_request;
        std::optional<Comet::AssetHandle> m_next_asset;
        unsigned m_material_active_item = 0;
        std::vector<std::shared_ptr<const Comet::MaterialLayout>> m_material_layouts;
        std::string m_asset_error;
        std::optional<AssetEdit> m_asset_edit;
        std::optional<Comet::AssetTarget> m_asset_read;
        std::optional<MaterialTemplateChange> m_template_change;
        mutable std::shared_ptr<const Comet::ShaderProgramArtifact> m_program_layout_source;
        mutable std::shared_ptr<const Comet::MaterialLayout> m_program_layout;
        mutable std::string m_program_layout_template;
        mutable std::string m_program_layout_error;
    };
}

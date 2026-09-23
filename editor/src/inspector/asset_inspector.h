#pragma once

#include "asset/database.h"
#include "assets/asset_edit.h"
#include "assets/material_editing.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Comet {
    class MaterialPrograms;
    class ShaderProgramArtifact;
}

namespace CometEditor {
    // 拥有资产编辑草稿；文件与 GPU 操作仍由宿主消费请求后执行。
    class AssetInspector {
    public:
        AssetInspector(
            const Comet::AssetDatabase& database, const Comet::MaterialPrograms& programs);
        void select(Comet::AssetHandle handle);
        void render(std::uint64_t generation, bool allow_drop);
        void set_material_layouts(
            std::vector<std::shared_ptr<const Comet::MaterialLayout>> layouts);
        [[nodiscard]] std::optional<AssetEdit> take_asset_edit();
        [[nodiscard]] std::optional<AssetRead> take_asset_read();
        void complete_asset_read(
            const AssetRead& request, Comet::Result<Comet::MaterialData> result);
        void complete_asset_edit(const AssetEdit& edit, bool succeeded, std::string error = {});

    private:
        void render_texture(const Comet::AssetRecord& record);
        void render_material(
            const Comet::AssetRecord& record, std::uint64_t generation, bool allow_drop);
        void confirm_material_template();
        void load_asset(const Comet::AssetRecord& record);
        void reimport_texture(const Comet::AssetRecord& record,
            const Comet::TextureImportSettings& previous_settings);
        void update_material(
            const Comet::AssetRecord& record, const Comet::MaterialData& previous_data);
        [[nodiscard]] std::string validate_material() const;
        [[nodiscard]] std::shared_ptr<const Comet::MaterialLayout> material_layout() const;
        [[nodiscard]] std::shared_ptr<const Comet::MaterialLayout> layout_for(
            Comet::AssetHandle program, const std::string& template_name) const;

        const Comet::AssetDatabase& m_asset_database;
        const Comet::MaterialPrograms& m_programs;
        Comet::AssetHandle m_selected_asset;
        Comet::AssetHandle m_loaded_asset;
        Comet::AssetRevision m_loaded_revision = 0;
        std::optional<Comet::TextureImportSettings> m_texture_import_settings;
        std::optional<Comet::MaterialData> m_material_data;
        std::vector<std::shared_ptr<const Comet::MaterialLayout>> m_material_layouts;
        std::string m_asset_error;
        std::optional<AssetEdit> m_asset_edit;
        std::optional<AssetRead> m_asset_read;
        std::optional<MaterialTemplateChange> m_template_change;
        mutable std::shared_ptr<const Comet::ShaderProgramArtifact> m_program_layout_source;
        mutable std::shared_ptr<const Comet::MaterialLayout> m_program_layout;
        mutable std::string m_program_layout_template;
        mutable std::string m_program_layout_error;
    };
}

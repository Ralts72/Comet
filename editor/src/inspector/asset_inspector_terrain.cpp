#include "inspector/asset_inspector.h"
#include <imgui.h>
#include "ui/widgets.h"

namespace CometEditor {
    void AssetInspector::complete_asset_read(
        const Comet::AssetTarget& request, Comet::Result<Comet::TerrainData> result) {
        if(request != m_loaded_target || m_selected_asset != request.handle
            || !m_asset_database.is_current(request))
            return;
        if(result) {
            m_terrain_data = std::move(result).value();
            m_terrain_saved = m_terrain_data;
        } else
            m_asset_error = result.error();
    }
    void AssetInspector::render_terrain() {
        if(!m_terrain_data)
            return;
        auto& data = *m_terrain_data;
        ImGui::TextWrapped(
            "导入时生成分块 LOD、混合贴图、静态碰撞与植被实例。路径相对于此 .terrain 文件；高度图首行对应 -Z。运行时无需原始高度图。");
        Ui::input_text("高度图###Heightmap", data.heightmap);
        ImGui::TextDisabled(
            "空路径使用内联高度；无内联高度时生成平地。高度图为单通道 8/16 位 PNG。");
        const auto select_power = [](const char* label, uint32_t& value, int first, int last,
                                      bool plus_one) {
            if(ImGui::BeginCombo(label, std::to_string(value).c_str())) {
                for(int power = first; power <= last; ++power) {
                    const uint32_t candidate = (1u << power) + uint32_t(plus_one);
                    if(ImGui::Selectable(std::to_string(candidate).c_str(), value == candidate))
                        value = candidate;
                }
                ImGui::EndCombo();
            }
        };
        select_power("高度分辨率", data.resolution, 2, 10, true);
        select_power("分块网格数", data.chunk_cells, 2, 7, false);
        ImGui::DragFloat("宽度（米）", &data.width, 1, 1, 100000);
        ImGui::DragFloat("深度（米）", &data.depth, 1, 1, 100000);
        ImGui::DragFloat("高度（米）", &data.height_scale, .1f, .01f, 100000);
        ImGui::DragFloat("裙边深度（米）", &data.skirt_depth, .1f, .01f, 100000);
        ImGui::Checkbox("静态网格碰撞", &data.collision);
        select_power("混合贴图分辨率", data.albedo_resolution, 4, 11, false);
        ImGui::SliderFloat("粗糙度", &data.roughness, 0, 1);
        constexpr const char* names[]{"低地", "陡坡", "高地"};
        for(size_t i = 0; i < data.layers.size(); ++i) {
            ImGui::PushID(int(i));
            if(ImGui::TreeNode(names[i])) {
                auto& layer = data.layers[i];
                ImGui::ColorEdit3("线性颜色", &layer.color.x);
                Ui::input_text("底色纹理", layer.texture);
                ImGui::DragFloat("纹理重复次数", &layer.tiling, .1f, .01f, 1024);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::SliderFloat("坡度混合起点（1-normal.y）", &data.slope_start, 0, 1);
        ImGui::SliderFloat("坡度混合终点", &data.slope_end, 0, 1);
        ImGui::SliderFloat("高地混合起点", &data.height_start, 0, 1);
        ImGui::SliderFloat("高地混合终点", &data.height_end, 0, 1);
        if(ImGui::TreeNode("植被散布")) {
            auto& grass = data.vegetation;
            int count = int(grass.count), seed = int(grass.seed);
            if(ImGui::SliderInt("数量上限", &count, 0, 10000))
                grass.count = uint32_t(count);
            if(ImGui::InputInt("随机种子", &seed))
                grass.seed = uint32_t(seed);
            ImGui::DragFloat("最小高度（米）", &grass.min_height, .05f, .01f, 100);
            ImGui::DragFloat("最大高度（米）", &grass.max_height, .05f, .01f, 100);
            ImGui::SliderFloat("最大坡度（度）", &grass.max_slope, 0, 89);
            ImGui::SliderFloat("最低海拔", &grass.min_elevation, 0, 1);
            ImGui::SliderFloat("最高海拔", &grass.max_elevation, 0, 1);
            ImGui::TreePop();
        }
        const auto valid = data.validate();
        if(!valid)
            ImGui::TextWrapped("%s", valid.error().c_str());
        ImGui::BeginDisabled(!valid || m_terrain_saved == m_terrain_data);
        if(ImGui::Button("保存并重新导入###Apply Terrain"))
            m_asset_edit = AssetEdit{m_loaded_target, TerrainEdit{*m_terrain_saved, data}};
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::Button("还原草稿###Reset Terrain"))
            data = *m_terrain_saved;
        ImGui::TextWrapped(
            "变更分块数量或植被数量后，从源模型更新实例以刷新实体层级；单独移动或替换实例可沿用模型覆盖。切换选择会丢弃未保存的地形草稿。");
    }
}

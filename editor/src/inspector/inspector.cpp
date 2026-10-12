#include "inspector/inspector.h"
#include "inspector/property_editor_registry.h"
#include "scene/selection.h"
#include "scene/scene_commands.h"
#include "scene/entity_reference.h"
#include "diagnostics/logger.h"

#include "scene/component_registry.h"
#include "scene/script_component.h"
#include "scene/scene_runtime.h"
#include "asset/registry.h"
#include "asset/script.h"
#include "scene/systems/script_system.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <string_view>
#include <span>
#include <utility>

namespace CometEditor {
    namespace {
        using Caption = std::pair<std::string_view, std::string_view>;

        std::optional<Comet::Math::Vec3> aligned_point_joint_anchor(
            Comet::Scene& scene, const Comet::Entity entity) {
            if(!entity.has_component<Comet::TransformComponent>())
                return std::nullopt;
            const auto& joint = entity.get_component<Comet::PointJointComponent>();
            auto position = scene.get_world_matrix(entity) * Comet::Math::Vec4(joint.anchor, 1);
            if(joint.connected_body) {
                const auto connected = scene.find_entity(joint.connected_body);
                if(!connected || !connected.has_component<Comet::TransformComponent>())
                    return std::nullopt;
                position = glm::inverse(scene.get_world_matrix(connected)) * position;
            }
            const Comet::Math::Vec3 anchor{position};
            if(!Comet::Math::is_finite(anchor))
                return std::nullopt;
            return anchor;
        }

        void set_caption(
            std::string& label, std::string_view id, std::span<const Caption> captions) {
            for(const auto& [key, caption] : captions) {
                if(key == id) {
                    label = std::string(caption) + "###" + label;
                    return;
                }
            }
        }

        std::vector<Comet::ComponentDescriptor> make_component_descriptions(
            const Comet::ComponentRegistry& source) {
            static constexpr Caption components[]{{"script", "脚本"}, {"name", "名称"},
                {"transform", "变换"}, {"mesh_renderer", "网格渲染"}, {"camera", "相机"},
                {"camera_controller", "相机控制器"}, {"audio_source", "音频源"},
                {"rigid_body", "刚体"}, {"collider", "碰撞体"}, {"point_joint", "点关节"},
                {"light", "光源"}};
            static constexpr Caption properties[]{{"script/asset", "脚本"},
                {"script/parameters", "参数"}, {"name/name", "名称"},
                {"transform/translation", "位置"}, {"transform/rotation", "旋转"},
                {"transform/scale", "缩放"}, {"mesh_renderer/mesh", "网格"},
                {"mesh_renderer/material", "材质"}, {"camera/primary", "主相机"},
                {"camera/projection", "投影方式"}, {"camera/fov", "透视视角"},
                {"camera/orthographic_height", "正交高度"}, {"camera/near_clip", "近裁剪面"},
                {"camera/far_clip", "远裁剪面"}, {"camera_controller/enabled", "启用"},
                {"camera_controller/move_speed", "移动速度"},
                {"camera_controller/look_sensitivity", "鼠标转向灵敏度"},
                {"camera_controller/look_speed", "持续转向速度 (度/秒)"},
                {"audio_source/clip", "音频片段"}, {"audio_source/play_on_start", "启动时播放"},
                {"audio_source/loop", "循环"}, {"audio_source/category", "音频分类"},
                {"audio_source/volume", "音量"}, {"rigid_body/motion", "运动类型"},
                {"rigid_body/mass_mode", "质量计算"}, {"rigid_body/mass", "质量 (kg)"},
                {"rigid_body/density", "密度 (kg/m³)"},
                {"rigid_body/continuous_collision", "连续碰撞 (CCD)"}, {"collider/shape", "形状"},
                {"collider/half_extents", "盒体半尺寸"}, {"collider/radius", "球体半径"},
                {"collider/is_trigger", "触发器"}, {"collider/friction", "摩擦系数"},
                {"collider/restitution", "弹性系数"}, {"point_joint/enabled", "启用"},
                {"point_joint/connected_body", "连接刚体"}, {"point_joint/anchor", "本地锚点"},
                {"point_joint/connected_anchor", "连接锚点"}, {"light/type", "类型"},
                {"light/enabled", "启用"}, {"light/casts_shadow", "投射阴影（方向光）"},
                {"light/color", "颜色（线性）"}, {"light/intensity", "强度"},
                {"light/range", "范围"}, {"light/inner_angle", "内角"},
                {"light/outer_angle", "外角"}};
            static constexpr Caption enum_options[]{{"perspective", "透视"},
                {"orthographic", "正交"}, {"effects", "音效"}, {"music", "音乐"},
                {"static", "静态"}, {"dynamic", "动态"}, {"kinematic", "运动学"}, {"box", "盒体"},
                {"sphere", "球体"}, {"directional", "方向光"}, {"point", "点光源"},
                {"spot", "聚光灯"}, {"mass", "指定质量"}, {"density", "按密度计算"}};

            const auto& descriptors = source.components();
            std::vector<Comet::ComponentDescriptor> result(descriptors.begin(), descriptors.end());
            for(auto& component : result) {
                set_caption(component.display_name, component.id, components);
                for(auto& property : component.properties) {
                    set_caption(
                        property.display_name, component.id + "/" + property.id, properties);
                    for(auto& option : property.enum_options)
                        set_caption(option.display_name, option.id, enum_options);
                }
            }
            return result;
        }

        constexpr PropertyEditTransaction::SceneTarget<Comet::SceneEnvironment> environment_target{
            &Comet::Scene::get_environment, &Comet::Scene::set_environment};
        constexpr PropertyEditTransaction::SceneTarget<Comet::PostProcessSettings>
            post_process_target{&Comet::Scene::get_post_process, &Comet::Scene::set_post_process};

    }

    InspectorPanel::InspectorPanel(const EditorState& state, SelectionService& selection,
        CommandHistory& history, PropertyEditTransaction& property_edit,
        const Comet::ComponentRegistry& component_registry,
        const PropertyEditorRegistry& property_editor_registry,
        const Comet::AssetDatabase& asset_database, const Comet::AssetRegistry& runtime_assets,
        const Comet::MaterialProgramValidator& programs, const Comet::SceneRuntime* runtime)
        : EditorPanel("属性###Inspector"), m_state(state), m_selection(selection),
          m_history(history), m_property_edit(property_edit),
          m_component_registry(component_registry),
          m_component_descriptions(make_component_descriptions(component_registry)),
          m_property_editor_registry(property_editor_registry), m_asset_database(asset_database),
          m_runtime_assets(runtime_assets), m_runtime(runtime),
          m_asset_inspector(asset_database, programs) {}

    void InspectorPanel::render() {
        m_asset_inspector.select(m_selection.get_selected_asset());
        m_asset_assignment.reset();
        if(m_property_edit.editing_scene()
            && (!m_user_visible || !m_selection.get_selected_scene()
                || m_selection.get_selected_scene() != m_history.get_scene()))
            static_cast<void>(finish_edit());
        if(!m_user_visible) {
            static_cast<void>(finish_edit());
            return;
        }

        if(!ImGui::Begin(window_label().c_str(), &m_user_visible)) {
            static_cast<void>(finish_edit());
            ImGui::End();
            return;
        }

        if(Comet::Entity entity = m_selection.get_selected_entity()) {
            render_entity(entity);
        } else if(m_selection.get_selected_asset()) {
            static_cast<void>(m_property_edit.commit());
            m_asset_inspector.render(m_history.generation(), m_state.mode == EditorMode::Edit);
        } else if(auto* scene = m_selection.get_selected_scene()) {
            if(!m_property_edit.editing_scene())
                static_cast<void>(finish_edit());
            render_scene(*scene);
        } else {
            static_cast<void>(m_property_edit.commit());
            ImGui::TextUnformatted("尚未选中实体或资产");
        }

        ImGui::End();
    }

    void InspectorPanel::render_entity(Comet::Entity entity) {
        bool active_property_visible = false;
        const bool edit_structure =
            m_state.mode == EditorMode::Edit && m_history.get_scene()
            && m_history.get_scene()->find_entity(entity.get_uuid()) == entity;
        const Comet::ComponentDescriptor* remove = nullptr;
        for(const Comet::ComponentDescriptor& component_descriptor : m_component_descriptions) {
            if(!component_descriptor.has_component(entity)) {
                continue;
            }

            ImGui::PushID(component_descriptor.id.c_str());
            if(component_descriptor.id == "name") {
                ImGui::PopID();
                continue;
            }
            const bool expanded = ImGui::CollapsingHeader(
                component_descriptor.display_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            if(ImGui::BeginPopupContextItem("Component actions")) {
                if(ImGui::MenuItem("移除组件###Remove Component", nullptr, false,
                       edit_structure
                           && SceneCommands::can_edit_component_structure(component_descriptor)))
                    remove = &component_descriptor;
                ImGui::EndPopup();
            }
            if(expanded) {
                for(const Comet::PropertyDescriptor& property : component_descriptor.properties) {
                    if(component_descriptor.id == "rigid_body") {
                        const auto mode =
                            entity.get_component<Comet::RigidBodyComponent>().mass_mode;
                        if((property.id == "mass" && mode == Comet::BodyMassMode::Density)
                            || (property.id == "density" && mode == Comet::BodyMassMode::Mass))
                            continue;
                    }
                    ImGui::PushID(property.id.c_str());
                    if(component_descriptor.id == "script"
                        && property.type == Comet::PropertyType::Parameters)
                        render_script_parameters(entity, component_descriptor, property);
                    else
                        render_property(entity, component_descriptor, property);
                    active_property_visible |= m_property_edit.targets(
                        {entity.get_uuid(), component_descriptor.id, property.id});
                    ImGui::PopID();
                }
            }
            ImGui::PopID();
        }
        if(!active_property_visible && !m_property_edit.commit()) {
            LOG_ERROR("Cannot finish hidden property edit");
            return;
        }
        const Comet::ComponentDescriptor* add = nullptr;
        ImGui::BeginDisabled(!edit_structure);
        if(ImGui::Button("添加组件###Add Component"))
            ImGui::OpenPopup("Add Component");
        if(ImGui::BeginPopup("Add Component")) {
            for(const auto& component : m_component_descriptions) {
                if(SceneCommands::can_edit_component_structure(component)
                    && !component.has_component(entity)
                    && ImGui::MenuItem(component.display_name.c_str()))
                    add = &component;
            }
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        // 结束本轮属性访问后再修改结构，避免移除正在访问的组件。
        if(add || remove) {
            if(!m_property_edit.commit()) {
                LOG_ERROR("Cannot finish property edit before component change");
                return;
            }
            bool changed = false;
            if(add)
                changed = SceneCommands::add_component(
                    m_history, m_component_registry, entity.get_uuid(), add->id);
            else
                changed = SceneCommands::remove_component(
                    m_history, m_component_registry, entity.get_uuid(), remove->id);
            if(!changed)
                LOG_ERROR("Cannot change component structure");
        }
    }

    void InspectorPanel::render_scene(Comet::Scene& scene) {
        render_environment(scene);
        render_post_process(scene);
    }

    template<typename Value>
    void InspectorPanel::apply_scene_edit(PropertyEditTransaction::SceneTarget<Value> target,
        const Value& value, const PropertyEditResult& result, bool can_edit) {
        if(!can_edit)
            return;
        if(result.active)
            m_active_item = ImGui::GetActiveID();
        if(ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            static_cast<void>(finish_edit(true));
            return;
        }
        if((result.began || result.changed) && !m_property_edit.begin(target))
            return;
        if(result.changed && !m_property_edit.preview(target, value))
            LOG_WARN("Invalid scene setting; previous preview retained");
        if(result.finished && m_property_edit.targets(target))
            static_cast<void>(finish_edit());
    }

    void InspectorPanel::render_environment(Comet::Scene& scene) {
        ImGui::SeparatorText("环境");
        const bool can_edit = m_state.mode == EditorMode::Edit && m_history.get_scene() == &scene;
        if(!can_edit)
            static_cast<void>(finish_edit(true));
        ImGui::BeginDisabled(!can_edit);
        auto environment = scene.get_environment();
        PropertyEditResult result;
        result.changed = edit_asset_reference("HDR 贴图###HDR map", environment.asset,
            m_asset_database, Comet::AssetType::Environment);
        if(const auto asset = accept_asset_drop(Comet::AssetType::Environment)) {
            environment.asset = asset->target.handle;
            result.changed = true;
        }
        result.changed |= ImGui::Checkbox("背景###Background", &environment.background);
        result.changed |= ImGui::Checkbox("光照###Lighting", &environment.lighting);
        result.finished = result.changed;
        result.include_item(ImGui::ColorEdit3("背景色###Background color",
            &environment.background_color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR));
        result.include_item(ImGui::DragFloat("强度###Intensity", &environment.intensity, 0.02f,
            0.0f, Comet::SceneEnvironment::MAX_INTENSITY, "%.2f", ImGuiSliderFlags_AlwaysClamp));
        result.include_item(ImGui::DragFloat("光照强度###Lighting intensity",
            &environment.lighting_intensity, 0.02f, 0.0f, Comet::SceneEnvironment::MAX_INTENSITY,
            "%.2f", ImGuiSliderFlags_AlwaysClamp));
        result.include_item(ImGui::DragFloat(
            "旋转###Rotation", &environment.rotation, 0.5f, 0.0f, 0.0f, "%.1f deg"));
        apply_scene_edit(environment_target, environment, result, can_edit);
        ImGui::EndDisabled();
    }

    void InspectorPanel::render_post_process(Comet::Scene& scene) {
        ImGui::SeparatorText("后处理");
        const bool can_edit = m_state.mode == EditorMode::Edit && m_history.get_scene() == &scene;
        ImGui::BeginDisabled(!can_edit);
        auto settings = scene.get_post_process();
        PropertyEditResult result;
        result.include_item(ImGui::DragFloat("曝光###Exposure", &settings.exposure, 0.02f, 0.0f,
            Comet::PostProcessSettings::MAX_EXPOSURE, "%.2f", ImGuiSliderFlags_AlwaysClamp));
        const bool toggled = ImGui::Checkbox("泛光###Bloom", &settings.bloom_enabled);
        result.changed |= toggled;
        result.finished |= toggled;
        ImGui::BeginDisabled(!settings.bloom_enabled);
        result.include_item(ImGui::DragFloat("泛光强度###Bloom strength", &settings.bloom_strength,
            0.01f, 0.0f, Comet::PostProcessSettings::MAX_BLOOM_STRENGTH, "%.2f",
            ImGuiSliderFlags_AlwaysClamp));
        result.include_item(ImGui::DragFloat("泛光阈值###Bloom threshold",
            &settings.bloom_threshold, 0.05f, 0.0f, Comet::PostProcessSettings::MAX_BLOOM_THRESHOLD,
            "%.2f", ImGuiSliderFlags_AlwaysClamp));
        ImGui::EndDisabled();
        apply_scene_edit(post_process_target, settings, result, can_edit);
        ImGui::EndDisabled();
    }

    bool InspectorPanel::finish_edit(const bool cancel) {
        m_asset_inspector.finish_material_edit(cancel);
        if(m_active_item && ImGui::GetCurrentContext() && ImGui::GetActiveID() == m_active_item)
            ImGui::ClearActiveID();
        m_active_item = 0;
        m_script_active_item = 0;
        if(cancel)
            return m_property_edit.cancel();
        return m_property_edit.commit();
    }

    void InspectorPanel::reset_for_scene_change() {
        m_asset_assignment.reset();
        m_script_edit_version.reset();
        if(!finish_edit(true))
            LOG_ERROR("Cannot cancel Inspector edit before changing scene");
    }

    void InspectorPanel::render_property(Comet::Entity entity,
        const Comet::ComponentDescriptor& component, const Comet::PropertyDescriptor& property) {
        if(!property.editable || property.read_only)
            return;
        auto value = property.copy_value(component.get_component(std::as_const(entity)));
        if(!value)
            return;
        if(property.type == Comet::PropertyType::AssetHandle && property.asset_type) {
            render_asset_property({entity.get_uuid(), component.id, property.id}, property,
                std::get<Comet::AssetHandle>(*value));
            return;
        }
        PropertyEditResult result;
        if(property.type == Comet::PropertyType::EntityReference) {
            std::optional<std::uint64_t> drop_generation;
            if(m_state.mode == EditorMode::Edit
                && m_history.get_scene() == &m_selection.get_scene())
                drop_generation = m_history.generation();
            result.changed = edit_entity_reference(property.display_name.c_str(),
                std::get<Comet::EntityUuid>(*value), m_selection.get_scene(), drop_generation);
            result.finished = result.changed;
        } else {
            if(!m_property_editor_registry.contains(property.type))
                return;
            result = std::visit(
                [&](auto& edited) {
                    return m_property_editor_registry.edit_property(property, &edited);
                },
                *value);
        }
        apply_property_edit(entity, component, property, *value, result);
        if(component.id == "point_joint" && property.id == "connected_body")
            ImGui::SetItemTooltip("选择另一刚体；无连接刚体时，连接锚点为固定世界位置。\n"
                                  "缺失实体／刚体会停用该连接；至少一端须为动态刚体。\n"
                                  "连接不自动关闭两物体之间的碰撞。");
        if(component.id == "point_joint" && property.id == "anchor")
            ImGui::SetItemTooltip("本实体的本地锚点，随缩放；点关节固定位置，允许自由转动。");
        if(component.id == "point_joint" && property.id == "connected_anchor") {
            ImGui::SetItemTooltip("连接到刚体时为对方的本地锚点，随其缩放；否则为固定世界位置。\n"
                                  "两锚点应在同一世界位置，否则求解器会拉动刚体使它们重合。\n"
                                  "右键可对齐到本地锚点；修改在下一固定步生效，保留当前速度。");
            if(ImGui::BeginPopupContextItem("Joint anchor actions")) {
                const auto aligned = aligned_point_joint_anchor(m_selection.get_scene(), entity);
                if(ImGui::MenuItem("对齐到本地锚点###Align joint anchor", nullptr, false,
                       aligned.has_value())) {
                    if(m_property_edit.commit())
                        apply_property_edit(entity, component, property, *aligned,
                            {.changed = true, .finished = true});
                    else
                        LOG_ERROR("Cannot finish property edit before aligning joint anchor");
                }
                ImGui::EndPopup();
            }
        }
        if(component.id == "rigid_body" && property.id == "mass_mode")
            ImGui::SetItemTooltip("指定质量：缩放保持质量，只改变形状与惯性。\n"
                                  "按密度计算：质量 = 密度 × 缩放后的碰撞体积，长度按米计算。\n"
                                  "仅动态刚体使用；下一固定步生效，切换保留当前速度。");
        if(component.id == "rigid_body" && property.id == "density")
            ImGui::SetItemTooltip("单位 kg/m³；默认 1000，水的近似密度。\n"
                                  "计算质量至少为 0.001 kg；尺寸加倍时体积与质量变为 8 倍。\n"
                                  "修改密度保留速度与接触身份，并唤醒动态刚体。");
        if(component.id == "rigid_body" && property.id == "continuous_collision")
            ImGui::SetItemTooltip("仅动态非触发器使用线性扫掠，减少高速穿过薄墙／地面的情况。\n"
                                  "下一固定步生效；切换保留当前速度。\n"
                                  "额外消耗模拟时间；高速旋转与触发区仍使用原有检测。");
        if(component.id == "collider" && property.id == "friction")
            ImGui::SetItemTooltip("范围 0～1，0 为无摩擦；两物体取摩擦系数的几何平均值。\n"
                                  "下一固定步生效，保留速度与接触；触发器不参与碰撞响应。");
        if(component.id == "collider" && property.id == "restitution")
            ImGui::SetItemTooltip(
                "范围 0～1，0 为无弹性反弹，1 为完全弹性；两物体取较大值。\n"
                "下一固定步生效；低速接触仍按后端阈值抑制反弹，触发器不参与碰撞响应。");
    }

    void InspectorPanel::render_script_parameters(Comet::Entity entity,
        const Comet::ComponentDescriptor& component, const Comet::PropertyDescriptor& property) {
        const auto& binding = entity.get_component<Comet::ScriptComponent>();
        std::shared_ptr<const Comet::Script> script;
        if(m_state.mode == EditorMode::Play) {
            if(m_runtime) {
                if(const auto* scripts = m_runtime->find_system<Comet::ScriptSystem>())
                    script = scripts->running_script(entity);
            }
        } else
            script = m_runtime_assets.resolve<Comet::Script>(binding.asset);
        const PropertyEditTransaction::Target target{entity.get_uuid(), component.id, property.id};
        if(m_script_edit_version != script) {
            if(m_property_edit.targets(target) && !m_property_edit.cancel()) {
                LOG_ERROR("Cannot cancel parameter edit after script definition changed");
                return;
            }
            if(m_script_active_item && ImGui::GetActiveID() == m_script_active_item)
                ImGui::ClearActiveID();
            if(m_active_item == m_script_active_item)
                m_active_item = 0;
            m_script_active_item = 0;
        }
        m_script_edit_version = script;
        if(!binding.asset)
            return;
        if(!script) {
            ImGui::TextDisabled("%s", "脚本未加载，请查看日志");
            return;
        }
        if(ImGui::Button("恢复默认参数###Restore default parameters")) {
            apply_property_edit(entity, component, property, Comet::ParameterMap{},
                {.changed = true, .finished = true});
            return;
        }
        if(auto checked = script->validate_overrides(binding.parameters); !checked) {
            ImGui::TextWrapped("%s", checked.error().message.c_str());
            auto compatible = binding.parameters;
            script->retain_compatible_overrides(compatible);
            if(compatible.size() != binding.parameters.size()
                && script->validate_overrides(compatible)
                && ImGui::Button("移除不兼容参数覆盖###Remove incompatible overrides")) {
                apply_property_edit(
                    entity, component, property, compatible, {.changed = true, .finished = true});
            }
            return;
        }
        auto overrides = binding.parameters;
        std::optional<std::uint64_t> drop_generation;
        if(m_state.mode == EditorMode::Edit && m_history.get_scene() == &m_selection.get_scene())
            drop_generation = m_history.generation();
        const auto result = m_property_editor_registry.edit_parameters(
            script->properties(), overrides, m_selection.get_scene(), drop_generation);
        apply_property_edit(entity, component, property, overrides, result);
        m_script_active_item = result.active ? m_active_item : 0;
    }

    void InspectorPanel::apply_property_edit(Comet::Entity entity,
        const Comet::ComponentDescriptor& component, const Comet::PropertyDescriptor& property,
        const Comet::PropertyValue& value, const PropertyEditResult& result) {
        const PropertyEditTransaction::Target target{entity.get_uuid(), component.id, property.id};
        const bool changed = result.changed;
        const bool active = result.active;
        const bool activated = result.began;
        const bool deactivated = result.finished;
        if(active) {
            m_active_item = result.active_item;
            if(!m_active_item)
                m_active_item = ImGui::GetItemID();
        }
        if(m_state.mode == EditorMode::Play) {
            // Play 中仍可调试 Runtime 属性，但不写入 Edit 文档历史。
            if(changed && !component.assign_property(entity, property.id, value)) {
                LOG_ERROR("Cannot update runtime property");
            }
            return;
        }

        if((activated || changed) && !m_property_edit.targets(target)) {
            if(!m_property_edit.begin(target)) {
                LOG_ERROR("Cannot begin edit of {}.{}", component.id, property.id);
                return;
            }
        }
        if(m_property_edit.targets(target)) {
            if(ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                if(!m_property_edit.cancel())
                    LOG_ERROR("Cannot cancel property edit");
                ImGui::ClearActiveID();
                return;
            }
            if(changed && !m_property_edit.preview(value)) {
                LOG_ERROR("Cannot preview edit of {}.{}", component.id, property.id);
            }
            if(deactivated || (changed && !active)) {
                if(!m_property_edit.commit())
                    LOG_ERROR("Cannot commit property edit");
            }
        }
    }

    std::optional<InspectorPanel::AssetAssignment> InspectorPanel::take_asset_assignment() {
        return std::exchange(m_asset_assignment, std::nullopt);
    }

    std::optional<AssetDragPayload> InspectorPanel::accept_asset_drop(
        const Comet::AssetType expected_type) {
        if(m_state.mode != EditorMode::Edit)
            return std::nullopt;
        return CometEditor::accept_asset_drop(
            m_asset_database, expected_type, m_history.generation());
    }

    void InspectorPanel::render_asset_property(const PropertyEditTransaction::Target& target,
        const Comet::PropertyDescriptor& property, const Comet::AssetHandle handle) {
        auto selected = handle;
        const auto type = *property.asset_type;
        if(edit_asset_reference(property.display_name.c_str(), selected, m_asset_database, type))
            m_asset_assignment =
                AssetAssignment{target, {{selected, m_asset_database.get_revision(selected)},
                                            m_history.generation(), type}};
        if(const auto asset = accept_asset_drop(type); asset && asset->target.handle != handle)
            m_asset_assignment = AssetAssignment{target, *asset};
    }

}

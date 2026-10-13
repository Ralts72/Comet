#include "render/resource/mesh.h"
#include "animation/animation_data.h"
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

        bool is_joint_component(const std::string_view id) {
            return id == "point_joint" || id == "hinge_joint" || id == "slider_joint";
        }

        template<typename Joint>
        bool joint_property_visible(const Joint& joint, const std::string_view id) {
            using namespace Comet;
            const bool position =
                joint.motor_enabled && joint.motor_mode == JointMotorMode::Position;
            if(id == "reference_axis" || id == "connected_reference_axis") {
                if constexpr(std::is_same_v<Joint, HingeJointComponent>)
                    return joint.limits_enabled || position;
                return true;
            }
            if(id == "min_angle" || id == "max_angle" || id == "min_distance"
                || id == "max_distance" || id == "limits_frequency")
                return joint.limits_enabled;
            if(id == "limits_damping")
                return joint.limits_enabled && joint.limits_frequency > 0;
            if(id == "motor_mode" || id == "motor_max_torque" || id == "motor_max_force")
                return joint.motor_enabled;
            if(id == "motor_speed")
                return joint.motor_enabled && joint.motor_mode == JointMotorMode::Velocity;
            if(id == "motor_target" || id == "motor_frequency" || id == "motor_damping")
                return position;
            return true;
        }

        std::optional<Comet::Math::Vec3> aligned_joint_value(Comet::Scene& scene,
            const Comet::Entity entity, const Comet::ComponentDescriptor& component,
            const std::string_view local_property) {
            using namespace Comet;
            const bool direction = local_property != "anchor";
            if(!entity.has_component<Comet::TransformComponent>())
                return std::nullopt;
            const auto* data = component.get_component(std::as_const(entity));
            const auto reference = component.find_property("connected_body")->copy_value(data);
            const auto local = component.find_property(local_property)->copy_value(data);
            if(!reference || !local)
                return std::nullopt;
            auto value = std::get<Math::Vec3>(*local);
            if(direction) {
                auto normalized = Math::normalized_direction(value);
                if(local_property == "reference_axis") {
                    const auto axis = component.find_property("axis")->copy_value(data);
                    if(!axis)
                        return std::nullopt;
                    normalized = Math::perpendicular_direction(std::get<Math::Vec3>(*axis), value);
                }
                if(!normalized)
                    return std::nullopt;
                value = *normalized;
            }
            const auto matrix = [&](Entity body) -> const Math::Mat4& {
                const auto& world = scene.get_world_matrix(body);
                if(direction)
                    return body.get_component<WorldTransformComponent>().pose_world_matrix;
                return world;
            };
            auto position = matrix(entity) * Math::Vec4(value, direction ? 0 : 1);
            const auto connected_body = std::get<EntityUuid>(*reference);
            if(connected_body) {
                const auto connected = scene.find_entity(connected_body);
                if(!connected || !connected.has_component<Comet::TransformComponent>())
                    return std::nullopt;
                position = glm::inverse(matrix(connected)) * position;
            }
            const Comet::Math::Vec3 anchor{position};
            if(local_property == "reference_axis") {
                const auto axis = component.find_property("connected_axis")->copy_value(data);
                if(!axis)
                    return std::nullopt;
                return Math::perpendicular_direction(std::get<Math::Vec3>(*axis), anchor);
            }
            if(direction)
                return Math::normalized_direction(anchor);
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
                {"character_controller", "角色控制器"}, {"rigid_body", "刚体"},
                {"collider", "碰撞体"}, {"point_joint", "点关节"}, {"hinge_joint", "铰链关节"},
                {"slider_joint", "滑动关节"}, {"light", "光源"}, {"animator", "动画"}};
            static constexpr Caption properties[]{{"animator/enabled", "启用"},
                {"animator/clip", "播放片段"}, {"animator/speed", "播放速度"},
                {"animator/loop", "循环"}, {"animator/transition", "过渡时间 (s)"},
                {"animator/character", "驱动角色"}, {"animator/idle", "待机动作"},
                {"animator/walk", "行走动作"}, {"animator/run", "跑步动作"},
                {"animator/crouch", "蹲伏动作"}, {"animator/crouch_walk", "蹲伏移动动作"},
                {"animator/jump", "跳跃动作"}, {"animator/fall", "下落动作"},
                {"script/asset", "脚本"}, {"script/parameters", "参数"}, {"name/name", "名称"},
                {"transform/translation", "位置"}, {"transform/rotation", "旋转"},
                {"transform/scale", "缩放"}, {"mesh_renderer/mesh", "网格"},
                {"mesh_renderer/material", "材质"}, {"camera/primary", "主相机"},
                {"camera/projection", "投影方式"}, {"camera/fov", "透视视角"},
                {"camera/orthographic_height", "正交高度"}, {"camera/near_clip", "近裁剪面"},
                {"camera/far_clip", "远裁剪面"}, {"camera_controller/enabled", "启用"},
                {"camera_controller/move_speed", "移动速度"},
                {"camera_controller/look_sensitivity", "鼠标转向灵敏度"},
                {"camera_controller/look_speed", "持续转向速度 (度/秒)"},
                {"camera_controller/mode", "控制模式"},
                {"camera_controller/orbit_distance", "环绕距离 (m)"},
                {"camera_controller/orbit_height", "中心高度 (m)"},
                {"camera_controller/orbit_min_distance", "最近距离 (m)"},
                {"camera_controller/orbit_max_distance", "最远距离 (m)"},
                {"camera_controller/orbit_zoom_step", "滚轮缩放步长 (m)"},
                {"camera_controller/collision_radius", "相机碰撞半径 (m)"},
                {"character_controller/enabled", "启用"},
                {"character_controller/radius", "胶囊半径 (m)"},
                {"character_controller/height", "胶囊总高 (m)"},
                {"character_controller/move_speed", "移动速度 (m/s)"},
                {"character_controller/run_speed", "跑步速度 (m/s)"},
                {"character_controller/crouch_speed", "蹲伏速度 (m/s)"},
                {"character_controller/crouch_height_ratio", "蹲伏高度比例"},
                {"character_controller/air_control", "空中操控"},
                {"character_controller/push_force", "推动力上限 (N)"},
                {"character_controller/jump_speed", "跳跃速度 (m/s)"},
                {"character_controller/max_slope_angle", "最大坡度 (°)"},
                {"character_controller/step_height", "台阶高度 (m)"},
                {"character_controller/ground_snap_distance", "地面吸附距离 (m)"},
                {"character_controller/gravity_scale", "重力倍率"},
                {"audio_source/clip", "音频片段"}, {"audio_source/play_on_start", "启动时播放"},
                {"audio_source/loop", "循环"}, {"audio_source/category", "音频分类"},
                {"audio_source/volume", "音量"}, {"rigid_body/motion", "运动类型"},
                {"rigid_body/mass_mode", "质量计算"}, {"rigid_body/mass", "质量 (kg)"},
                {"rigid_body/density", "密度 (kg/m³)"},
                {"rigid_body/continuous_collision", "连续碰撞 (CCD)"}, {"collider/shape", "形状"},
                {"collider/half_extents", "盒体半尺寸"}, {"collider/radius", "球体半径"},
                {"collider/is_trigger", "触发器"}, {"collider/friction", "摩擦系数"},
                {"collider/restitution", "弹性系数"}, {"light/type", "类型"},
                {"light/enabled", "启用"}, {"light/casts_shadow", "投射阴影（方向光）"},
                {"light/color", "颜色（线性）"}, {"light/intensity", "强度"},
                {"light/range", "范围"}, {"light/inner_angle", "内角"},
                {"light/outer_angle", "外角"}};
            static constexpr Caption enum_options[]{{"perspective", "透视"},
                {"orthographic", "正交"}, {"effects", "音效"}, {"music", "音乐"},
                {"static", "静态"}, {"dynamic", "动态"}, {"kinematic", "运动学"}, {"box", "盒体"},
                {"sphere", "球体"}, {"directional", "方向光"}, {"point", "点光源"},
                {"spot", "聚光灯"}, {"mass", "指定质量"}, {"density", "按密度计算"},
                {"velocity", "速度"}, {"position", "位置"}, {"free", "自由"}, {"orbit", "环绕"}};
            static constexpr Caption joint_properties[]{{"enabled", "启用"},
                {"connected_body", "连接刚体"}, {"anchor", "本地锚点"},
                {"connected_anchor", "连接锚点"}, {"axis", "本地转轴"},
                {"connected_axis", "连接转轴"}, {"limits_enabled", "角度限位"},
                {"reference_axis", "本地零度方向"}, {"connected_reference_axis", "连接零度方向"},
                {"min_angle", "最小角度 (°)"}, {"max_angle", "最大角度 (°)"},
                {"limits_frequency", "限位频率 (Hz)"}, {"limits_damping", "限位阻尼比"},
                {"motor_enabled", "启用驱动"}, {"motor_mode", "驱动模式"},
                {"motor_target", "目标角度 (°)"}, {"motor_speed", "目标角速度 (°/s)"},
                {"motor_max_torque", "最大力矩 (N·m)"}, {"motor_frequency", "驱动频率 (Hz)"},
                {"motor_damping", "驱动阻尼比"}, {"break_force", "断裂力 (N)"},
                {"break_torque", "断裂力矩 (N·m)"}};
            static constexpr Caption slider_properties[]{{"axis", "本地滑动轴"},
                {"connected_axis", "连接滑动轴"}, {"reference_axis", "本地朝向"},
                {"connected_reference_axis", "连接朝向"}, {"limits_enabled", "行程限位"},
                {"min_distance", "最小行程 (m)"}, {"max_distance", "最大行程 (m)"},
                {"motor_target", "目标行程 (m)"}, {"motor_speed", "目标速度 (m/s)"},
                {"motor_max_force", "最大驱动力 (N)"}};

            const auto& descriptors = source.components();
            std::vector<Comet::ComponentDescriptor> result(descriptors.begin(), descriptors.end());
            for(auto& component : result) {
                set_caption(component.display_name, component.id, components);
                for(auto& property : component.properties) {
                    if(is_joint_component(component.id))
                        set_caption(property.display_name, property.id, joint_properties);
                    else
                        set_caption(
                            property.display_name, component.id + "/" + property.id, properties);
                    if(component.id == "slider_joint")
                        set_caption(property.display_name, property.id, slider_properties);
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

    InspectorPanel::~InspectorPanel() {
        reset_animation_preview();
    }

    void InspectorPanel::render() {
        m_animation_preview_drawn = false;
        m_asset_inspector.select(m_selection.get_selected_asset());
        m_asset_assignment.reset();
        if(m_property_edit.editing_scene()
            && (!m_user_visible || !m_selection.get_selected_scene()
                || m_selection.get_selected_scene() != m_history.get_scene()))
            static_cast<void>(finish_edit());
        if(!m_user_visible) {
            reset_animation_preview();
            static_cast<void>(finish_edit());
            return;
        }

        if(!ImGui::Begin(window_label().c_str(), &m_user_visible)) {
            reset_animation_preview();
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

        if(!m_animation_preview_drawn)
            reset_animation_preview();
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
                if(component_descriptor.id == "animator")
                    render_animation_preview(entity);
                if(component_descriptor.id == "character_controller")
                    ImGui::TextWrapped(
                        "脚底为原点，总高须大于直径。角色与刚体不能共用同一实体；保持竖直、无父级和正等比缩放。");
                for(const Comet::PropertyDescriptor& property : component_descriptor.properties) {
                    if(component_descriptor.id == "camera_controller"
                        && (property.id.starts_with("orbit_") || property.id == "collision_radius")
                        && entity.get_component<Comet::CameraControllerComponent>().mode
                               != Comet::CameraControllerComponent::Mode::Orbit)
                        continue;
                    if(component_descriptor.id == "rigid_body") {
                        const auto mode =
                            entity.get_component<Comet::RigidBodyComponent>().mass_mode;
                        if((property.id == "mass" && mode == Comet::BodyMassMode::Density)
                            || (property.id == "density" && mode == Comet::BodyMassMode::Mass))
                            continue;
                    }
                    if(component_descriptor.id == "hinge_joint"
                        && !joint_property_visible(
                            entity.get_component<Comet::HingeJointComponent>(), property.id))
                        continue;
                    if(component_descriptor.id == "slider_joint"
                        && !joint_property_visible(
                            entity.get_component<Comet::SliderJointComponent>(), property.id))
                        continue;
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
                const bool conflicting =
                    (component.id == "rigid_body"
                        && entity.has_component<Comet::CharacterControllerComponent>())
                    || (component.id == "character_controller"
                        && entity.has_component<Comet::RigidBodyComponent>());
                if(SceneCommands::can_edit_component_structure(component)
                    && !component.has_component(entity)
                    && ImGui::MenuItem(
                        component.display_name.c_str(), nullptr, false, !conflicting))
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
        reset_animation_preview();
        m_asset_assignment.reset();
        m_script_edit_version.reset();
        if(!finish_edit(true))
            LOG_ERROR("Cannot cancel Inspector edit before changing scene");
    }

    void InspectorPanel::reset_animation_preview() {
        if(m_preview_publication) {
            auto& scene = m_selection.get_scene();
            const auto target = m_animation_preview.target();
            if(target.scene_lifetime == scene.get_lifetime()) {
                const auto entity = scene.find_entity(target.entity);
                if(scene.get_animation_playback(entity) == m_preview_publication)
                    scene.set_animation_playback(entity, nullptr);
            }
        }
        m_animation_preview.reset();
        m_preview_publication.reset();
        m_animation_preview_drawn = false;
    }

    bool InspectorPanel::bind_animation_preview(Comet::Entity entity) {
        auto& scene = m_selection.get_scene();
        if(m_state.mode != EditorMode::Edit || (m_runtime && m_runtime->is_active()) || !entity
            || !entity.has_component<Comet::AnimatorComponent>()
            || !entity.has_component<Comet::MeshRendererComponent>()) {
            reset_animation_preview();
            return false;
        }
        const auto mesh = m_runtime_assets.resolve<Comet::Mesh>(
            entity.get_component<Comet::MeshRendererComponent>().mesh);
        if(!mesh || !mesh->animation_data() || mesh->animation_data()->clips.empty()) {
            reset_animation_preview();
            return false;
        }
        const AnimationPreview::Target target{scene.get_lifetime(), entity.get_uuid()};
        if(m_animation_preview.target() != target)
            reset_animation_preview();
        m_animation_preview.bind(
            target, mesh->animation_data(), entity.get_component<Comet::AnimatorComponent>().clip);
        return true;
    }

    void InspectorPanel::update_animation_preview(double delta_time) {
        if(!m_animation_preview_drawn) {
            reset_animation_preview();
            return;
        }
        const auto entity = m_selection.get_selected_entity();
        if(!bind_animation_preview(entity))
            return;
        m_animation_preview.advance(delta_time);
        m_preview_publication = m_animation_preview.snapshot();
        m_selection.get_scene().set_animation_playback(entity, m_preview_publication);
    }

    void InspectorPanel::render_animation_preview(Comet::Entity entity) {
        if(m_state.mode != EditorMode::Edit)
            return;
        ImGui::SeparatorText("动画预览");
        if(!bind_animation_preview(entity)) {
            ImGui::TextDisabled("需要已加载且包含动作片段的骨骼网格");
            return;
        }
        m_animation_preview_drawn = true;
        const auto* selected = m_animation_preview.clip();
        if(ImGui::BeginCombo("预览片段###Animation Preview Clip", selected->name.c_str())) {
            for(const auto& clip : m_animation_preview.animation()->clips)
                if(ImGui::Selectable(clip.name.c_str(), &clip == selected))
                    static_cast<void>(m_animation_preview.select_clip(clip.name));
            ImGui::EndCombo();
        }
        selected = m_animation_preview.clip();
        ImGui::BeginDisabled(selected->duration <= 0);
        if(m_animation_preview.playing()) {
            if(ImGui::Button("暂停预览###Pause Animation Preview"))
                m_animation_preview.pause();
        } else if(ImGui::Button("播放预览###Play Animation Preview")) {
            static_cast<void>(m_animation_preview.play());
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::Button("结束预览###Stop Animation Preview"))
            m_animation_preview.stop();
        ImGui::SameLine();
        bool loop = m_animation_preview.loop();
        if(ImGui::Checkbox("循环###Loop Animation Preview", &loop))
            m_animation_preview.set_loop(loop);
        float time = float(m_animation_preview.time());
        ImGui::BeginDisabled(selected->duration <= 0);
        if(ImGui::SliderFloat("时间 (s)###Animation Preview Time", &time, 0, selected->duration,
               "%.3f", ImGuiSliderFlags_AlwaysClamp))
            static_cast<void>(m_animation_preview.seek(time));
        ImGui::EndDisabled();
        float speed = m_animation_preview.speed();
        if(ImGui::DragFloat("预览速度###Animation Preview Speed", &speed, 0.05f, 0, 4, "%.2f",
               ImGuiSliderFlags_AlwaysClamp))
            static_cast<void>(m_animation_preview.set_speed(speed));
        ImGui::TextDisabled(
            "时长 %.3f s；拖动时间会暂停，结束预览恢复绑定姿态。", selected->duration);
        ImGui::SeparatorText("播放配置");
    }

    void InspectorPanel::render_property(Comet::Entity entity,
        const Comet::ComponentDescriptor& component, const Comet::PropertyDescriptor& property) {
        if(!property.editable || property.read_only)
            return;
        auto value = property.copy_value(component.get_component(std::as_const(entity)));
        if(!value)
            return;
        if(component.id == "animator") {
            const bool driven = bool(entity.get_component<Comet::AnimatorComponent>().character);
            const bool motion = property.id == "idle" || property.id == "walk"
                                || property.id == "run" || property.id == "crouch"
                                || property.id == "crouch_walk" || property.id == "jump"
                                || property.id == "fall";
            if((driven && (property.id == "clip" || property.id == "loop")) || (!driven && motion))
                return;
        }
        const bool joint = is_joint_component(component.id);
        const bool align_joint =
            joint
            && (property.id == "connected_anchor" || property.id == "connected_axis"
                || property.id == "connected_reference_axis");
        if(property.type == Comet::PropertyType::AssetHandle && property.asset_type) {
            render_asset_property({entity.get_uuid(), component.id, property.id}, property,
                std::get<Comet::AssetHandle>(*value));
            return;
        }
        PropertyEditResult result;
        if(component.id == "animator" && property.type == Comet::PropertyType::String
            && entity.has_component<Comet::MeshRendererComponent>()) {
            auto mesh = m_runtime_assets.resolve<Comet::Mesh>(
                entity.get_component<Comet::MeshRendererComponent>().mesh);
            if(mesh && mesh->animation_data()) {
                auto& clip = std::get<std::string>(*value);
                const auto& clips = mesh->animation_data()->clips;
                std::string preview = clip;
                if(clip.empty())
                    preview = "绑定姿态";
                else if(!mesh->animation_data()->find_clip(clip))
                    preview = "片段已丢失：" + clip;
                if(ImGui::BeginCombo(property.display_name.c_str(), preview.c_str())) {
                    if(ImGui::Selectable("绑定姿态", clip.empty()) && !clip.empty()) {
                        clip.clear();
                        result.changed = true;
                    }
                    for(const auto& candidate : clips)
                        if(ImGui::Selectable(candidate.name.c_str(), candidate.name == clip)
                            && candidate.name != clip) {
                            clip = candidate.name;
                            result.changed = true;
                        }
                    ImGui::EndCombo();
                }
                result.finished = result.changed;
                apply_property_edit(entity, component, property, *value, result);
                return;
            }
        }
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
            if(align_joint) {
                const auto& style = ImGui::GetStyle();
                const float button_width = ImGui::CalcTextSize("对齐").x + style.FramePadding.x * 2;
                const float label_width =
                    ImGui::CalcTextSize(property.display_name.c_str(), nullptr, true).x
                    + style.ItemInnerSpacing.x;
                ImGui::SetNextItemWidth(
                    std::max(1.0f, ImGui::GetContentRegionAvail().x - label_width
                                       - style.ItemSpacing.x - button_width));
            }
            result = std::visit(
                [&](auto& edited) {
                    return m_property_editor_registry.edit_property(property, &edited);
                },
                *value);
        }
        apply_property_edit(entity, component, property, *value, result);
        if(joint && property.id == "connected_body")
            ImGui::SetItemTooltip("选择另一刚体；无连接刚体时，连接锚点为固定世界位置。\n"
                                  "缺失实体／刚体会停用该连接；至少一端须为动态刚体。\n"
                                  "连接不自动关闭两物体之间的碰撞。");
        if(joint && (property.id == "break_force" || property.id == "break_torque"))
            ImGui::SetItemTooltip("0 关闭对应断裂判断；正值限制每个固定步的关节载荷。\n"
                                  "超过任一阈值后取消本关节的启用，保留当步产生的运动。\n"
                                  "重新勾选启用可重连；Play 断裂只修改运行副本，日志记录载荷。");
        if(component.id == "point_joint" && property.id == "anchor")
            ImGui::SetItemTooltip("本实体的本地锚点，随缩放；点关节固定位置，允许自由转动。");
        if(component.id == "hinge_joint" && property.id == "anchor")
            ImGui::SetItemTooltip("本实体的本地锚点，随缩放；铰链固定位置，仅允许绕转轴旋转。");
        if(joint && property.id == "connected_anchor")
            ImGui::SetItemTooltip(
                "连接到刚体时为对方的本地锚点，随其缩放；否则为固定世界位置。\n"
                "两锚点应在同一世界位置，否则求解器会拉动刚体使它们重合。\n"
                "旁边的对齐按钮可对齐到本地锚点；修改在下一固定步生效，保留当前速度。");
        if(component.id == "hinge_joint" && property.id == "axis")
            ImGui::SetItemTooltip("本实体的本地转轴，只受旋转影响，不随缩放。\n"
                                  "方向须非零且有限，模拟会归一化；可启用角度限位与驱动。");
        if(component.id == "hinge_joint" && property.id == "connected_axis")
            ImGui::SetItemTooltip("连接到刚体时为对方的本地方向，否则为世界方向；不随缩放。\n"
                                  "两端转轴应在世界中同向，否则求解器会转动刚体使它们对齐。\n"
                                  "旁边的对齐按钮可对齐到本地转轴；修改在下一固定步生效。");
        if(component.id == "hinge_joint"
            && (property.id == "reference_axis" || property.id == "connected_reference_axis"))
            ImGui::SetItemTooltip(
                "参考方向投影到对应转轴的垂直平面，不能为零或与转轴平行。\n"
                "两端零度方向在世界中同向时角度为零；只受旋转影响。\n"
                "连接零度方向可用旁边的对齐按钮设置，保存后不会在刚体重建时重新定义。");
        if(component.id == "hinge_joint" && property.id == "limits_enabled")
            ImGui::SetItemTooltip("约束本实体相对连接端的旋转角度；正方向遵循本地转轴的右手方向。\n"
                                  "范围须包含零：最小值 -180～0°，最大值 0～180°，两者不能相等。");
        if(component.id == "hinge_joint" && property.id == "motor_speed")
            ImGui::SetItemTooltip(
                "本实体相对连接端的目标角速度，单位 °/s；正方向遵循本地转轴的右手方向。\n"
                "0 会制动相对旋转；驱动受最大力矩和角度限位约束。");
        if(component.id == "hinge_joint" && property.id == "motor_max_torque")
            ImGui::SetItemTooltip("驱动能够施加的最大力矩，单位 N·m；0 表示不施加驱动力矩。\n"
                                  "受负载影响，实际角速度可能达不到目标；修改在下一固定步生效。");
        if(component.id == "slider_joint"
            && (property.id == "axis" || property.id == "connected_axis"))
            ImGui::SetItemTooltip("只允许沿滑动轴相对平移，锁定其余相对平移与两端相对朝向。\n"
                                  "本地轴属于本实体；连接轴属于对方，连接世界时为世界方向。\n"
                                  "两端应在世界中同向，方向非零且有限；只受旋转影响。");
        if(component.id == "slider_joint"
            && (property.id == "reference_axis" || property.id == "connected_reference_axis"))
            ImGui::SetItemTooltip("朝向投影到滑动轴的垂直平面，不能为零或与滑动轴平行。\n"
                                  "两端在世界中同向时为锁定朝向；只受旋转影响。\n"
                                  "先对齐连接滑动轴，再对齐连接朝向，避免启动时突然转动。");
        if(component.id == "slider_joint" && property.id == "limits_enabled")
            ImGui::SetItemTooltip("锚点重合时行程为零，正方向沿本地滑动轴，单位米。\n"
                                  "最小值须 ≤0，最大值须 ≥0，两者不能相等；行程不随缩放。");
        if(component.id == "slider_joint" && property.id == "motor_speed")
            ImGui::SetItemTooltip("本实体相对连接端的目标速度，单位 m/s；正方向沿本地滑动轴。\n"
                                  "0 会制动相对平移；实际速度受负载、最大驱动力和行程限位约束。");
        if(component.id == "slider_joint" && property.id == "motor_max_force")
            ImGui::SetItemTooltip("驱动能够施加的最大力，单位 N；0 表示不施加驱动力。\n"
                                  "修改在下一固定步生效，暂停时等待继续或单步。");
        if(joint && property.id == "motor_mode")
            ImGui::SetItemTooltip("速度：持续趋近目标角速度／速度。\n"
                                  "位置：以弹簧和阻尼趋近目标角度／行程，受最大力矩／驱动力限制。");
        if(joint && property.id == "motor_target")
            ImGui::SetItemTooltip("相对保存的零度方向／零行程位置；超出启用的限位时限制到边界。\n"
                                  "目标预览为绿色，实际姿态受负载、弹簧和最大驱动力影响。");
        if(joint && property.id == "limits_frequency")
            ImGui::SetItemTooltip("0 使用硬限位；正值使用弹性限位，单位 Hz。\n"
                                  "软限位允许越界，越界后施加弹簧回拉；频率越高响应越硬。");
        if(joint && property.id == "motor_frequency")
            ImGui::SetItemTooltip("位置驱动弹簧的频率，单位 Hz，最小 0.001。\n"
                                  "频率越高回到目标越快，实际施力仍受最大力矩／驱动力限制。");
        if(joint && (property.id == "motor_damping" || property.id == "limits_damping"))
            ImGui::SetItemTooltip(
                "阻尼比：0 无弹簧阻尼，低于 1 可振荡，1 为临界阻尼，高于 1 回稳更慢。\n"
                "软限位的阻尼只在越界时生效；刚体本身的阻尼独立于此参数。");
        if(align_joint) {
            const bool direction = property.id != "connected_anchor";
            const auto aligned = aligned_joint_value(m_selection.get_scene(), entity, component,
                std::string_view(property.id).substr(std::string_view("connected_").size()));
            ImGui::SameLine();
            ImGui::BeginDisabled(!aligned);
            if(ImGui::Button("对齐###Align joint")) {
                if(m_property_edit.commit())
                    apply_property_edit(
                        entity, component, property, *aligned, {.changed = true, .finished = true});
                else
                    LOG_ERROR("Cannot finish property edit before aligning joint");
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                direction ? "将连接方向对齐到本地方向。" : "将连接锚点对齐到本地锚点的世界位置。");
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

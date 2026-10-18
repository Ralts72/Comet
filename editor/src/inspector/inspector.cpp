#include "animation/animation_controller.h"
#include "render/resource/mesh.h"
#include "animation/animation_data.h"
#include "inspector/inspector.h"
#include <cctype>
#include <tuple>
#include "assets/reflection_bake.h"
#include "assets/irradiance_bake.h"
#include "assets/navigation_bake.h"
#include "inspector/property_editor_registry.h"
#include "scene/selection.h"
#include "scene/scene_commands.h"
#include "scene/entity_reference.h"
#include "diagnostics/logger.h"
#include "common/scope_exit.h"

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
                position = Math::inverse(matrix(connected)) * position;
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

        bool matches_filter(std::string_view text, std::string_view filter) {
            return filter.empty()
                   || std::search(text.begin(), text.end(), filter.begin(), filter.end(),
                          [](unsigned char a, unsigned char b) {
                              return std::tolower(a) == std::tolower(b);
                          })
                          != text.end();
        }
        std::string_view parameter_group(std::string_view component, std::string_view id) {
            if(component == "particle_emitter") {
                if(id.find("collision") != id.npos || id == "restitution" || id == "friction")
                    return "碰撞";
                if(id.find("color") != id.npos || id.find("size") != id.npos || id == "texture"
                    || id == "blend" || id.find("atlas") != id.npos || id.find("soft") != id.npos)
                    return "外观";
                if(id.find("wind") != id.npos || id.find("vortex") != id.npos
                    || id.find("turbulence") != id.npos || id.find("field") != id.npos
                    || id == "acceleration" || id == "drag")
                    return "运动与力场";
                return "发射";
            }
            if(component == "irradiance_volume") {
                if(id.find("budget") != id.npos || id.find("rays") != id.npos
                    || id.find("history") != id.npos)
                    return "更新与质量";
                if(id.find("bias") != id.npos || id.find("visibility") != id.npos)
                    return "防漏光";
                return "区域与来源";
            }
            if(is_joint_component(component)) {
                if(id.find("motor") != id.npos)
                    return "驱动";
                if(id.find("reference_axis") != id.npos || id.find("limit") != id.npos
                    || id.starts_with("min_") || id.starts_with("max_"))
                    return "限位";
                if(id.starts_with("break_"))
                    return "断裂";
                return "连接与坐标";
            }
            if(component == "audio_source") {
                if(id.find("distance") != id.npos || id == "spatial" || id == "rolloff"
                    || id == "occlusion")
                    return "空间与遮挡";
                if(id.starts_with("fade") || id.starts_with("reverb"))
                    return "淡入淡出与混响";
                return "播放";
            }
            return {};
        }

        std::vector<Comet::ComponentDescriptor> make_component_descriptions(
            const Comet::ComponentRegistry& source) {
            static constexpr Caption components[]{{"script", "脚本"}, {"name", "名称"},
                {"transform", "变换"}, {"mesh_renderer", "网格渲染"}, {"camera", "相机"},
                {"camera_controller", "相机控制器"}, {"audio_source", "音频源"},
                {"particle_emitter", "粒子发射器"}, {"audio_listener", "音频监听者"},
                {"character_controller", "角色控制器"}, {"rigid_body", "刚体"},
                {"collider", "碰撞体"}, {"point_joint", "点关节"}, {"hinge_joint", "铰链关节"},
                {"slider_joint", "滑动关节"}, {"light", "光源"}, {"animator", "动画"},
                {"character_animation", "角色动画驱动"}};
            static constexpr Caption properties[]{{"particle_emitter/texture", "粒子纹理"},
                {"particle_emitter/blend", "混合方式"}, {"particle_emitter/enabled", "启用发射"},
                {"particle_emitter/capacity", "最大粒子数"}, {"particle_emitter/seed", "随机种子"},
                {"particle_emitter/burst", "启动爆发数量"}, {"particle_emitter/rate", "每秒发射数"},
                {"particle_emitter/lifetime", "寿命（秒）"},
                {"particle_emitter/speed", "初速度（米/秒）"},
                {"particle_emitter/spread", "锥体半角（度）"},
                {"particle_emitter/acceleration", "世界加速度"},
                {"particle_emitter/start_color", "出生颜色"},
                {"particle_emitter/end_color", "结束颜色"},
                {"particle_emitter/start_size", "出生尺寸（米）"},
                {"particle_emitter/end_size", "结束尺寸（米）"}, {"animator/enabled", "启用"},
                {"animator/clip", "播放片段"}, {"animator/speed", "播放速度"},
                {"animator/loop", "循环"}, {"animator/transition", "过渡时间 (s)"},
                {"animator/controller", "动画控制器"}, {"animator/parameters", "参数"},
                {"character_animation/character", "驱动角色"}, {"script/asset", "脚本"},
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
                {"audio_source/volume", "音量"}, {"audio_source/spatial", "空间音效"},
                {"audio_source/min_distance", "开始衰减距离（米）"},
                {"audio_source/max_distance", "最大距离（米）"},
                {"audio_source/rolloff", "衰减强度"}, {"audio_listener/enabled", "启用"},
                {"rigid_body/motion", "运动类型"}, {"rigid_body/mass_mode", "质量计算"},
                {"rigid_body/mass", "质量 (kg)"}, {"rigid_body/density", "密度 (kg/m³)"},
                {"rigid_body/continuous_collision", "连续碰撞 (CCD)"}, {"collider/shape", "形状"},
                {"collider/collision_layer", "所属碰撞层"},
                {"collider/collision_mask", "检测碰撞层"},
                {"character_controller/collision_layer", "所属碰撞层"},
                {"character_controller/collision_mask", "检测碰撞层"},
                {"collider/half_extents", "盒体半尺寸"}, {"collider/radius", "球／胶囊半径"},
                {"collider/height", "胶囊总高度"}, {"collider/mesh", "碰撞网格"},
                {"collider/is_trigger", "触发器"}, {"collider/friction", "摩擦系数"},
                {"collider/restitution", "弹性系数"}, {"light/type", "类型"},
                {"light/enabled", "启用"}, {"light/casts_shadow", "投射阴影"},
                {"light/color", "颜色（线性）"}, {"light/intensity", "强度"},
                {"light/range", "范围"}, {"light/inner_angle", "内角"},
                {"light/outer_angle", "外角"}};
            static constexpr Caption enum_options[]{{"perspective", "透视"},
                {"orthographic", "正交"}, {"effects", "音效"}, {"music", "音乐"},
                {"static", "静态"}, {"dynamic", "动态"}, {"kinematic", "运动学"}, {"box", "盒体"},
                {"sphere", "球体"}, {"capsule", "胶囊"}, {"convex_hull", "凸包"},
                {"triangle_mesh", "静态三角网格"}, {"directional", "方向光"}, {"point", "点光源"},
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
                    property.group = parameter_group(component.id, property.id);
                    if(property.id == "history_weight")
                        property.description = "历史权重越大越稳定，但光照变化响应越慢。";
                    if(property.id == "update_budget")
                        property.description =
                            "每帧更新数量；与场景 GI 总预算共同限制 GPU 工作量。";
                    if(property.id == "max_translation")
                        property.description = "骨盆相对基础姿态允许的最大偏移，单位为米。";
                    if(property.id == "query_budget")
                        property.description =
                            "单粒子遍历预算；超限会终止该视觉粒子，避免无界 GPU 工作。";
                    if(property.id == "seed")
                        property.description = "固定种子使相同初始配置可重复生成。";
                    if(property.id == "wind_amplitude")
                        property.description = "风动幅度；改变视觉形变，不回写刚体或角色碰撞体。";
                }
                const auto group_order = [](std::string_view group) {
                    constexpr std::array groups{std::string_view{}, std::string_view("发射"),
                        std::string_view("区域与来源"), std::string_view("播放"),
                        std::string_view("连接与坐标"), std::string_view("外观"),
                        std::string_view("运动与力场"), std::string_view("碰撞"),
                        std::string_view("更新与质量"), std::string_view("防漏光"),
                        std::string_view("限位"), std::string_view("驱动"),
                        std::string_view("断裂"), std::string_view("淡入淡出与混响"),
                        std::string_view("空间与遮挡")};
                    return std::ranges::find(groups, group) - groups.begin();
                };
                std::stable_sort(component.properties.begin(), component.properties.end(),
                    [&](const auto& a, const auto& b) {
                        if(a.group != b.group)
                            return group_order(a.group) < group_order(b.group);
                        const auto toggle = [](const auto& p) {
                            return p.id == "limits_enabled" || p.id == "motor_enabled";
                        };
                        return toggle(a) && !toggle(b);
                    });
            }
            return result;
        }

        constexpr PropertyEditTransaction::SceneTarget<Comet::SceneEnvironment> environment_target{
            &Comet::Scene::get_environment, &Comet::Scene::set_environment};
        constexpr PropertyEditTransaction::SceneTarget<Comet::PostProcessSettings>
            post_process_target{&Comet::Scene::get_post_process, &Comet::Scene::set_post_process};

    }

    namespace {
        template<typename Value>
        Value merge_edited_value(const Value& before, const Value& after, Value current) {
            if(before.index() != after.index() || current.index() != after.index())
                return after;
            std::visit(
                [&](auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr(std::is_same_v<T, Comet::Math::Vec3>
                                 || std::is_same_v<T, Comet::Math::Vec4>) {
                        const auto& previous = std::get<T>(before);
                        const auto& next = std::get<T>(after);
                        for(int axis = 0; axis < T::length(); ++axis)
                            if(previous[axis] != next[axis])
                                value[axis] = next[axis];
                    } else
                        value = std::get<T>(after);
                },
                current);
            return current;
        }

        Comet::ParameterMap merge_parameters(const Comet::Script& script,
            const Comet::ParameterMap& before, const Comet::ParameterMap& after,
            Comet::ParameterMap current, std::span<const std::string> changed) {
            for(const auto& name : changed) {
                const auto edited = after.find(name);
                if(edited == after.end()) {
                    current.erase(name);
                    continue;
                }
                const auto& value = edited->second;
                const auto previous = before.find(name);
                const auto& fallback = script.properties().at(name).default_value;
                const auto local = current.find(name);
                current.insert_or_assign(
                    name, merge_edited_value(previous == before.end() ? fallback : previous->second,
                              value, local == current.end() ? fallback : local->second));
            }
            return current;
        }

        bool property_visible(
            Comet::Entity entity, std::string_view component, std::string_view property) {
            if(component == "irradiance_volume") {
                const auto realtime =
                    entity.get_component<Comet::IrradianceVolumeComponent>().realtime;
                if(property == "irradiance" || property == "ray_samples" || property == "bounces")
                    return !realtime;
                if(property == "update_budget" || property == "update_rays"
                    || property == "history_weight")
                    return realtime;
            }
            if(component == "animator") {
                const bool controlled =
                    bool(entity.get_component<Comet::AnimatorComponent>().controller);
                if(property == "clip" || property == "loop" || property == "transition")
                    return !controlled;
                if(property == "parameters")
                    return controlled;
            }
            if(component == "collider") {
                const auto& collider = entity.get_component<Comet::ColliderComponent>();
                if(property == "mesh")
                    return collider.uses_mesh();
                if(property == "half_extents")
                    return collider.shape == Comet::ColliderShape::Box;
                if(property == "height")
                    return collider.shape == Comet::ColliderShape::Capsule;
                if(property == "radius")
                    return collider.shape == Comet::ColliderShape::Sphere
                           || collider.shape == Comet::ColliderShape::Capsule;
            }
            if(component == "camera_controller"
                && (property.starts_with("orbit_") || property == "collision_radius"))
                return entity.get_component<Comet::CameraControllerComponent>().mode
                       == Comet::CameraControllerComponent::Mode::Orbit;
            if(component == "rigid_body") {
                const auto mode = entity.get_component<Comet::RigidBodyComponent>().mass_mode;
                if(property == "mass")
                    return mode != Comet::BodyMassMode::Density;
                if(property == "density")
                    return mode == Comet::BodyMassMode::Density;
            }
            if(component == "hinge_joint")
                return joint_property_visible(
                    entity.get_component<Comet::HingeJointComponent>(), property);
            if(component == "slider_joint")
                return joint_property_visible(
                    entity.get_component<Comet::SliderJointComponent>(), property);
            return true;
        }

        std::string mixed_label(std::string_view label) {
            const auto split = label.find("###");
            const auto visible = label.substr(0, split);
            const auto id = split == std::string_view::npos ? label : label.substr(split + 3);
            return std::string(visible) + "（多值）###" + std::string(id);
        }
    }

    InspectorPanel::InspectorPanel(const EditorState& state, SelectionService& selection,
        CommandHistory& history, PropertyEditTransaction& property_edit,
        const Comet::ComponentRegistry& component_registry,
        const PropertyEditorRegistry& property_editor_registry,
        const Comet::AssetDatabase& asset_database, const Comet::AssetRegistry& runtime_assets,
        const Comet::MaterialProgramValidator& programs, const Comet::SceneRuntime* runtime,
        ReflectionBake* reflection_bake, IrradianceBake* irradiance_bake,
        NavigationBake* navigation_bake, Comet::PhysicsService::MeshSource preview_meshes)
        : EditorPanel("属性###Inspector"), m_state(state), m_selection(selection),
          m_history(history), m_property_edit(property_edit),
          m_component_registry(component_registry),
          m_component_descriptions(make_component_descriptions(component_registry)),
          m_property_editor_registry(property_editor_registry), m_asset_database(asset_database),
          m_runtime_assets(runtime_assets), m_runtime(runtime), m_reflection_bake(reflection_bake),
          m_irradiance_bake(irradiance_bake), m_navigation_bake(navigation_bake),
          m_asset_inspector(asset_database, programs),
          m_particle_preview(std::move(preview_meshes)) {}

    InspectorPanel::~InspectorPanel() {
        reset_animation_preview();
        reset_particle_preview();
    }

    void InspectorPanel::render() {
        m_animation_preview_drawn = false;
        m_particle_preview_drawn = false;
        const Comet::ScopeExit finish_preview([&] {
            render_controller_editor();
            if(!m_animation_preview_drawn)
                reset_animation_preview();
            if(!m_particle_preview_drawn)
                reset_particle_preview();
        });
        auto selected = m_selection.get_selected_entities();
        if(selected != m_entities) {
            if(!finish_edit())
                return;
            m_entities = std::move(selected);
        }
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

        if(m_asset_inspector.animation_selection_pending()) {
            m_asset_inspector.render(m_history.generation(), m_state.mode == EditorMode::Edit);
        } else if(Comet::Entity entity = m_selection.get_selected_entity()) {
            if(m_entities.size() > 1)
                ImGui::TextWrapped("共同属性 · %zu 个实体。多值以主目标显示，修改一轴保留其他轴。",
                    m_entities.size());
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

        if(auto selected = m_asset_inspector.take_selection_request())
            m_selection.select_asset(*selected);
        ImGui::End();
    }

    void InspectorPanel::render_entity(Comet::Entity entity) {
        bool active_property_visible = false;
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("###Parameter filter", "搜索参数或组件", m_property_filter.data(),
            m_property_filter.size());
        const std::string_view filter(m_property_filter.data());
        const bool edit_structure =
            m_entities.size() == 1 && m_state.mode == EditorMode::Edit && m_history.get_scene()
            && m_history.get_scene()->find_entity(entity.get_uuid()) == entity;
        const Comet::ComponentDescriptor* remove = nullptr;
        for(const Comet::ComponentDescriptor& component_descriptor : m_component_descriptions) {
            if(!std::ranges::all_of(m_entities, [&](auto uuid) {
                   return component_descriptor.has_component(
                       m_selection.get_scene().find_entity(uuid));
               }))
                continue;

            const bool component_matches = matches_filter(component_descriptor.display_name, filter)
                                           || matches_filter(component_descriptor.id, filter);
            if(!component_matches
                && std::ranges::none_of(component_descriptor.properties, [&](const auto& property) {
                       return matches_filter(property.display_name, filter)
                              || matches_filter(property.id, filter);
                   }))
                continue;
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
                if(component_descriptor.id == "animator" && m_entities.size() == 1)
                    render_animation_preview(entity);
                if(m_entities.size() == 1
                    && (component_descriptor.id == "particle_emitter"
                        || (component_descriptor.id == "trail"
                            && !entity.has_component<Comet::ParticleEmitterComponent>())))
                    render_particle_preview(entity);
                if(component_descriptor.id == "character_controller")
                    ImGui::TextWrapped(
                        "脚底为原点，总高须大于直径。角色与刚体不能共用同一实体；保持竖直、无父级和正等比缩放。");
                if(component_descriptor.id == "collider"
                    && entity.get_component<Comet::ColliderComponent>().uses_mesh())
                    ImGui::TextWrapped(
                        "凸包适合可移动刚体；三角网格仅支持静态刚体，三角形正面参与碰撞。"
                        "使用网格原点与绑定姿态，支持正非等比缩放，不随骨骼动画变形。");
                std::string_view group;
                bool group_open = true;
                for(const Comet::PropertyDescriptor& property : component_descriptor.properties) {
                    if(!component_matches && !matches_filter(property.display_name, filter)
                        && !matches_filter(property.id, filter))
                        continue;
                    if((component_descriptor.id == "reflection_probe"
                           || component_descriptor.id == "irradiance_volume")
                        && property.id == "bake_fingerprint")
                        continue;
                    if(!std::ranges::all_of(m_entities, [&](auto uuid) {
                           return property_visible(m_selection.get_scene().find_entity(uuid),
                               component_descriptor.id, property.id);
                       }))
                        continue;
                    if(property.group != group) {
                        if(!group.empty() && group_open)
                            ImGui::Unindent();
                        group = property.group;
                        group_open = group.empty()
                                     || ImGui::TreeNodeEx(
                                         group.data(), ImGuiTreeNodeFlags_DefaultOpen
                                                           | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                        if(!group.empty() && group_open)
                            ImGui::Indent();
                    }
                    if(!group_open)
                        continue;
                    ImGui::PushID(property.id.c_str());
                    if(component_descriptor.id == "script"
                        && property.type == Comet::PropertyType::Parameters)
                        render_script_parameters(entity, component_descriptor, property);
                    else if(component_descriptor.id == "animator"
                            && property.type == Comet::PropertyType::Parameters)
                        render_animation_parameters(entity, component_descriptor, property);
                    else
                        render_property(entity, component_descriptor, property);
                    if(!property.description.empty()
                        && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("%s", property.description.c_str());
                    active_property_visible |= m_property_edit.targets_many(
                        property_targets(component_descriptor.id, property.id));
                    ImGui::PopID();
                }
                if(!group.empty() && group_open)
                    ImGui::Unindent();
                if(component_descriptor.id == "navigation_region")
                    render_navigation_bake(entity, edit_structure);
                if(component_descriptor.id == "reflection_probe")
                    render_reflection_bake(entity, edit_structure);
                if(component_descriptor.id == "irradiance_volume") {
                    if(entity.get_component<Comet::IrradianceVolumeComponent>().realtime)
                        ImGui::TextWrapped(
                            "实时模式按预算更新灯光与静态／蒙皮动态几何的间接光。移动网格可改变遮挡；"
                            "关闭“参与 GI 几何”后仅采样。预算越小、历史权重越大，响应越慢。");
                    else
                        render_irradiance_bake(entity, edit_structure);
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
        if(m_entities.size() > 1 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("添加或移除组件请先选择单个实体");
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

    void InspectorPanel::render_navigation_bake(Comet::Entity entity, bool can_edit) {
        if(m_entities.size() != 1 || !m_navigation_bake)
            return;
        ImGui::TextWrapped(
            "%s", m_navigation_bake->status(m_selection.get_scene(), entity.get_uuid()).c_str());
        ImGui::BeginDisabled(!can_edit || m_navigation_bake->busy());
        if(ImGui::Button("烘焙导航###Bake Navigation") && finish_edit())
            m_bake_request = {
                BakeRequest::Kind::Navigation, entity.get_uuid(), m_history.generation()};
        ImGui::EndDisabled();
        if(m_navigation_bake->busy()) {
            ImGui::SameLine();
            if(ImGui::Button("取消烘焙###Cancel Navigation Bake"))
                m_navigation_bake->cancel();
        }
        ImGui::TextWrapped(
            "使用静态、非触发碰撞形状；运行时障碍按占用变化更新导航，并参与局部避障。");
    }

    void InspectorPanel::render_reflection_bake(Comet::Entity entity, bool can_edit) {
        if(m_entities.size() != 1 || !m_reflection_bake)
            return;
        ImGui::TextWrapped(
            "%s", m_reflection_bake->status(m_selection.get_scene(), entity.get_uuid()).c_str());
        ImGui::BeginDisabled(!can_edit || m_reflection_bake->busy());
        if(ImGui::Button("烘焙反射###Bake Reflection") && finish_edit())
            m_bake_request = {
                BakeRequest::Kind::Reflection, entity.get_uuid(), m_history.generation()};
        ImGui::EndDisabled();
        ImGui::TextWrapped(
            "捕获静态网格与光源；不包含动画角色、动态刚体、粒子、雾和其他反射探针。");
    }

    void InspectorPanel::render_irradiance_bake(Comet::Entity entity, bool can_edit) {
        if(m_entities.size() != 1 || !m_irradiance_bake)
            return;
        ImGui::TextWrapped(
            "%s", m_irradiance_bake->status(m_selection.get_scene(), entity.get_uuid()).c_str());
        ImGui::BeginDisabled(!can_edit || m_irradiance_bake->busy());
        if(ImGui::Button("烘焙 GI###Bake GI") && finish_edit())
            m_bake_request = {
                BakeRequest::Kind::Irradiance, entity.get_uuid(), m_history.generation()};
        ImGui::EndDisabled();
        if(m_irradiance_bake->busy()) {
            ImGui::SameLine();
            if(ImGui::Button("取消###Cancel GI Bake"))
                m_irradiance_bake->cancel();
            const auto progress = m_irradiance_bake->progress();
            const float ratio = progress.total ? float(progress.completed) / progress.total : 0;
            ImGui::ProgressBar(ratio);
            ImGui::Text("探针 %u / %u", progress.completed, progress.total);
        }
        if(const auto& report = m_irradiance_bake->last_report(); report) {
            ImGui::TextWrapped("上次烘焙 %.2f 秒，%u 个有效探针，%u 个三角形", report->seconds,
                report->statistics.active_probes, report->statistics.triangles);
            ImGui::TextWrapped("输入 %.2f MiB，求解器 %.2f MiB，运行纹理 %.2f KiB",
                report->input_bytes / 1048576.0, report->statistics.working_bytes / 1048576.0,
                report->product_bytes / 1024.0);
        }
        ImGui::TextWrapped(
            "区域轴对齐，半尺寸与间距使用世界单位；静态内容烘焙，移动物体仅采样。修改静态内容或移动区域后需重新烘焙。");
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
        int gi_budget = int(environment.realtime_gi_budget);
        result.include_item(ImGui::DragInt("实时 GI 总预算###GI total budget", &gi_budget, 1, 1,
            16384, "%d 探针/帧", ImGuiSliderFlags_AlwaysClamp));
        environment.realtime_gi_budget = uint32_t(gi_budget);
        ImGui::Spacing();
        const bool fog_changed = ImGui::Checkbox("距离雾###Distance fog", &environment.fog_enabled);
        result.changed |= fog_changed;
        result.finished |= fog_changed;
        if(ImGui::IsItemHovered())
            ImGui::SetTooltip("按相机前向距离混合网格与雾色；天空背景和粒子暂不受影响。");
        ImGui::BeginDisabled(!environment.fog_enabled);
        result.include_item(ImGui::ColorEdit3("雾颜色（线性）###Fog color",
            &environment.fog_color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR));
        result.include_item(ImGui::DragFloat("起始距离###Fog start", &environment.fog_start, 0.1f,
            0.0f, environment.fog_end - 0.01f, "%.2f m", ImGuiSliderFlags_AlwaysClamp));
        result.include_item(ImGui::DragFloat("完全覆盖距离###Fog end", &environment.fog_end, 0.1f,
            environment.fog_start + 0.01f, Comet::SceneEnvironment::MAX_FOG_DISTANCE, "%.2f m",
            ImGuiSliderFlags_AlwaysClamp));
        ImGui::EndDisabled();
        ImGui::Spacing();
        auto& volume = environment.volumetric_fog;
        const bool volume_changed = ImGui::Checkbox("体积高度雾###Volumetric fog", &volume.enabled);
        result.changed |= volume_changed;
        result.finished |= volume_changed;
        if(ImGui::IsItemHovered())
            ImGui::SetTooltip("相机空间体素积分；灯光、阴影、网格和粒子共用雾层。");
        if(volume.enabled) {
            result.include_item(ImGui::ColorEdit3(
                "介质颜色###Volume color", &volume.color.x, ImGuiColorEditFlags_Float));
            result.include_item(ImGui::DragFloat("密度###Volume density", &volume.density, .001f, 0,
                2, "%.3f", ImGuiSliderFlags_AlwaysClamp));
            result.include_item(ImGui::DragFloat("基准高度###Volume height", &volume.base_height,
                .1f, -100000, 100000, "%.2f m", ImGuiSliderFlags_AlwaysClamp));
            result.include_item(ImGui::DragFloat("高度衰减###Volume falloff",
                &volume.height_falloff, .01f, 0, 10, "%.2f", ImGuiSliderFlags_AlwaysClamp));
            result.include_item(ImGui::DragFloat("前向散射###Volume anisotropy", &volume.anisotropy,
                .01f, -.9f, .9f, "%.2f", ImGuiSliderFlags_AlwaysClamp));
            result.include_item(ImGui::DragFloat("最大积分距离###Volume distance",
                &volume.max_distance, 1, 1, 1000, "%.0f m", ImGuiSliderFlags_AlwaysClamp));
            result.include_item(ImGui::DragFloat("环境散射强度###Volume ambient", &volume.ambient,
                .01f, 0, 16, "%.2f", ImGuiSliderFlags_AlwaysClamp));
        }
        result.include_item(ImGui::DragFloat3("世界风（米／秒）###World wind", &environment.wind.x,
            .1f, -50, 50, "%.2f", ImGuiSliderFlags_AlwaysClamp));
        apply_scene_edit(environment_target, environment, result, can_edit);
        ImGui::EndDisabled();
    }

    void InspectorPanel::render_post_process(Comet::Scene& scene) {
        ImGui::SeparatorText("画面效果");
        const bool can_edit = m_state.mode == EditorMode::Edit && m_history.get_scene() == &scene;
        ImGui::BeginDisabled(!can_edit);
        auto settings = scene.get_post_process();
        PropertyEditResult result;
        result.include_item(ImGui::DragFloat("曝光###Exposure", &settings.exposure, 0.02f, 0.0f,
            Comet::PostProcessSettings::MAX_EXPOSURE, "%.2f", ImGuiSliderFlags_AlwaysClamp));
        const bool weighted =
            ImGui::Checkbox("加权透明混合（OIT）###Weighted OIT", &settings.weighted_oit);
        result.changed |= weighted;
        result.finished |= weighted;
        if(ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "近似处理烟雾和内置透明网格交错；额外使用累积目标。关闭可与深度排序对照。");
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
        const bool ao_toggled =
            ImGui::Checkbox("环境遮蔽###Ambient occlusion", &settings.ao_enabled);
        result.changed |= ao_toggled;
        result.finished |= ao_toggled;
        if(ImGui::IsItemHovered())
            ImGui::SetTooltip("屏幕空间遮蔽只影响环境光；需启用环境光照，直接光和自发光保持不变。");
        ImGui::BeginDisabled(!settings.ao_enabled);
        result.include_item(ImGui::DragFloat("遮蔽半径###AO radius", &settings.ao_radius, 0.01f,
            0.01f, 10.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp));
        result.include_item(ImGui::DragFloat("遮蔽强度###AO strength", &settings.ao_strength, 0.01f,
            0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp));
        ImGui::EndDisabled();
        auto& reflections = settings.reflections;
        const bool reflection_changed =
            ImGui::Checkbox("屏幕空间反射###Screen space reflections", &reflections.enabled);
        result.changed |= reflection_changed;
        result.finished |= reflection_changed;
        if(ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "需在画质设置选择 Deferred；离屏或遮挡命中保留反射探针／IBL，透明表面不参与。");
        if(reflections.enabled) {
            result.include_item(ImGui::DragFloat("反射强度###SSR strength", &reflections.strength,
                .01f, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp));
            result.include_item(ImGui::DragFloat("追踪距离###SSR distance",
                &reflections.max_distance, .1f, .1f, 200, "%.1f m", ImGuiSliderFlags_AlwaysClamp));
            result.include_item(ImGui::DragFloat("命中厚度###SSR thickness", &reflections.thickness,
                .005f, .001f, 2, "%.3f m", ImGuiSliderFlags_AlwaysClamp));
            result.include_item(ImGui::DragFloat("最大粗糙度###SSR roughness",
                &reflections.max_roughness, .01f, .05f, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp));
        }
        if(ImGui::TreeNodeEx("镜头效果###Lens", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& lens = settings.lens;
            const bool focus_changed =
                ImGui::Checkbox("景深###Depth of field", &lens.depth_of_field);
            result.changed |= focus_changed;
            result.finished |= focus_changed;
            ImGui::BeginDisabled(!lens.depth_of_field);
            result.include_item(ImGui::DragFloat("对焦距离###Focus distance", &lens.focus_distance,
                .05f, .01f, 100000, "%.2f m", ImGuiSliderFlags_AlwaysClamp));
            result.include_item(ImGui::DragFloat("清晰范围###Focus range", &lens.focus_range, .05f,
                .01f, 100000, "%.2f m", ImGuiSliderFlags_AlwaysClamp));
            result.include_item(
                ImGui::SliderFloat("弥散半径###Aperture", &lens.aperture, 0, 32, "%.1f px"));
            ImGui::EndDisabled();
            const bool motion_changed =
                ImGui::Checkbox("运动模糊###Motion blur", &lens.motion_blur);
            result.changed |= motion_changed;
            result.finished |= motion_changed;
            ImGui::BeginDisabled(!lens.motion_blur);
            result.include_item(
                ImGui::SliderFloat("快门比例###Shutter", &lens.shutter, 0, 1, "%.2f"));
            result.include_item(
                ImGui::SliderFloat("最大拖影###Maximum blur", &lens.max_blur, 0, 64, "%.1f px"));
            ImGui::EndDisabled();
            ImGui::TextDisabled("景深使用相机距离；运动模糊包含相机与物体运动。UI 保持清晰。");
            ImGui::TreePop();
        }
        if(ImGui::TreeNode("路径追踪（App／Play 共用）###Runtime trace")) {
            auto& trace = settings.path_trace;
            const auto toggle = [&](const char* label, bool& value) {
                const bool changed = ImGui::Checkbox(label, &value);
                result.changed |= changed;
                result.finished |= changed;
            };
            toggle("场景使用路径追踪###Scene tracing", trace.enabled);
            toggle("动态模式###Scene dynamic tracing", trace.dynamic);
            toggle("GPU 时空降噪###Scene trace denoise", trace.denoise);
            for(const auto& [label, target, minimum, maximum] :
                {std::tuple{"样本上限###Trace samples", &trace.samples, 1, 4096},
                    {"反弹上限###Trace bounces", &trace.bounces, 1, 8},
                    {"最长边像素###Trace resolution", &trace.max_dimension, 32, 2048}}) {
                int value = int(*target);
                result.include_item(ImGui::SliderInt(label, &value, minimum, maximum));
                *target = uint32_t(value);
            }
            result.include_item(
                ImGui::SliderFloat("历史权重###Trace history", &trace.history_weight, 0, .98f));
            result.include_item(
                ImGui::SliderFloat("均匀介质密度###Medium density", &trace.medium_density, 0, 2));
            result.include_item(
                ImGui::ColorEdit3("散射反照率###Medium albedo", &trace.medium_color.x));
            result.include_item(ImGui::SliderFloat(
                "散射各向异性###Medium anisotropy", &trace.medium_anisotropy, -.9f, .9f));
            ImGui::TextWrapped(
                "动态模式每帧重新采样；软件 BVH 的实际帧率取决于场景和分辨率。静态模式逐帧累积。");
            ImGui::TreePop();
        }
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
        reset_particle_preview();
        m_asset_assignment.reset();
        m_bake_request.reset();
        m_script_edit_version.reset();
        m_entities.clear();
        if(!finish_edit(true))
            LOG_ERROR("Cannot cancel Inspector edit before changing scene");
    }

    void InspectorPanel::reset_animation_preview() {
        m_animation_preview.reset(&m_selection.get_scene());
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
        if(!mesh || !mesh->animation_data()) {
            reset_animation_preview();
            return false;
        }
        const AnimationPreview::Target target{scene.get_lifetime(), entity.get_uuid()};
        if(m_animation_preview.target() != target)
            reset_animation_preview();
        m_animation_preview.bind(
            target, mesh->animation_data(), entity.get_component<Comet::AnimatorComponent>().clip);
        scene.set_animation_source(entity, mesh->animation_data());
        return true;
    }

    void InspectorPanel::render_controller_editor() {
        auto& assets = m_asset_inspector;
        if(!assets.controller_active())
            return;
        auto& editor = assets.controller_editor();
        std::vector<AnimationControllerEditor::Model> models;
        auto& scene = m_selection.get_scene();
        scene.each<const Comet::AnimatorComponent, const Comet::MeshRendererComponent>(
            [&](Comet::Entity entity, const auto& animator, const auto& renderer) {
                auto mesh = m_runtime_assets.resolve<Comet::Mesh>(renderer.mesh);
                if(!mesh || !mesh->animation_data())
                    return;
                models.push_back(
                    {entity.get_uuid(), entity.get_component<Comet::NameComponent>().name,
                        animator.controller, mesh->animation_data(),
                        scene.get_animation_playback(entity), animator.parameters});
            });
        const bool runtime = m_state.mode != EditorMode::Edit;
        editor.set_models(std::move(models), assets.controller_handle(), runtime);
        AnimationPreview* preview = nullptr;
        if(editor.is_open() && !runtime && editor.model()) {
            const auto entity = scene.find_entity(editor.model()->entity);
            if(bind_animation_preview(entity)) {
                preview = &m_animation_preview;
                m_animation_preview_drawn = true;
            }
        }
        assets.render_controller_window(preview);
        if(!editor.is_open())
            m_animation_preview_drawn = false;
    }

    void InspectorPanel::reset_particle_preview() {
        m_particle_preview.reset(&m_selection.get_scene());
        m_particle_preview_drawn = false;
    }

    bool InspectorPanel::bind_particle_preview(Comet::Entity entity) {
        auto& scene = m_selection.get_scene();
        if(m_state.mode != EditorMode::Edit || (m_runtime && m_runtime->is_active()) || !entity
            || (!entity.has_component<Comet::ParticleEmitterComponent>()
                && !entity.has_component<Comet::TrailComponent>())) {
            reset_particle_preview();
            return false;
        }
        const ParticlePreview::Target target{scene.get_lifetime(), entity.get_uuid()};
        if(m_particle_preview.target() != target)
            reset_particle_preview();
        (void)scene.get_world_matrix(entity);
        const auto& world = entity.get_component<Comet::WorldTransformComponent>();
        auto pose = world.pose_world_matrix;
        pose[3] = world.world_matrix[3];
        Comet::ParticleEmitterComponent settings;
        settings.enabled = false;
        if(entity.has_component<Comet::ParticleEmitterComponent>())
            settings = entity.get_component<Comet::ParticleEmitterComponent>();
        const auto* trail = entity.has_component<Comet::TrailComponent>()
                                ? &entity.get_component<Comet::TrailComponent>()
                                : nullptr;
        auto bound = m_particle_preview.bind(target, settings, pose, &scene, trail);
        if(!bound) {
            reset_particle_preview();
            return false;
        }
        return true;
    }

    void InspectorPanel::render_particle_preview(Comet::Entity entity) {
        if(m_state.mode != EditorMode::Edit)
            return;
        const bool emitter = entity.has_component<Comet::ParticleEmitterComponent>();
        ImGui::SeparatorText(emitter ? "粒子预览" : "拖尾预览");
        if(emitter && ImGui::BeginCombo("应用预设", "选择火花／烟雾／雨／雪")) {
            using Comet::ParticlePreset;
            for(const auto& [name, kind] : {std::pair{"火花", ParticlePreset::Sparks},
                    std::pair{"烟雾", ParticlePreset::Smoke}, std::pair{"雨", ParticlePreset::Rain},
                    std::pair{"雪", ParticlePreset::Snow}}) {
                if(!ImGui::Selectable(name))
                    continue;
                const auto settings = Comet::ParticleEmitterComponent::preset(kind);
                const auto component = m_component_registry.find_component("particle_emitter");
                if(!component)
                    continue;
                std::vector<PropertyEditTransaction::Target> targets;
                std::vector<Comet::PropertyValue> values;
                for(const auto& property : component->properties) {
                    auto value = property.copy_value(&settings);
                    if(!value)
                        continue;
                    for(const auto uuid : m_entities) {
                        targets.push_back({uuid, component->id, property.id});
                        values.push_back(*value);
                    }
                }
                apply_property_values(targets, values, {.changed = true, .finished = true});
            }
            ImGui::EndCombo();
        }
        if(!bind_particle_preview(entity)) {
            ImGui::TextDisabled("粒子配置无效，请检查参数");
            return;
        }
        m_particle_preview_drawn = true;
        if(m_particle_preview.playing()) {
            if(ImGui::Button("暂停###Pause Particle Preview"))
                m_particle_preview.pause();
        } else if(ImGui::Button("播放###Play Particle Preview")) {
            if(auto played = m_particle_preview.play(); !played)
                LOG_ERROR("Particle preview: {}", played.error().message);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(
            !emitter || !entity.get_component<Comet::ParticleEmitterComponent>().enabled);
        if(ImGui::Button("爆发###Burst Particle Preview"))
            if(auto emitted = m_particle_preview.burst(); !emitted)
                LOG_ERROR("Particle preview: {}", emitted.error().message);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::Button("单步###Step Particle Preview"))
            if(auto stepped = m_particle_preview.step(); !stepped)
                LOG_ERROR("Particle preview: {}", stepped.error().message);
        ImGui::SameLine();
        if(ImGui::Button("结束###Stop Particle Preview"))
            m_particle_preview.stop();
        const auto frame = m_particle_preview.snapshot();
        ImGui::TextDisabled(
            "存活 %zu；预览不运行游戏脚本，也不写入场景。", frame ? frame->instances.size() : 0);
        if(entity.has_component<Comet::TrailComponent>())
            ImGui::TextDisabled("播放后移动实体可观察拖尾。");
        ImGui::SeparatorText("发射配置");
    }

    void InspectorPanel::update_previews(double delta_time) {
        update_animation_preview(delta_time);
        if(!m_particle_preview_drawn) {
            reset_particle_preview();
            return;
        }
        if(!bind_particle_preview(m_selection.get_selected_entity()))
            return;
        if(auto advanced = m_particle_preview.advance(delta_time); !advanced) {
            LOG_ERROR("Particle preview: {}", advanced.error().message);
            reset_particle_preview();
            return;
        }
        m_particle_preview.publish_to(m_selection.get_scene());
    }

    void InspectorPanel::update_animation_preview(double delta_time) {
        if(!m_animation_preview_drawn) {
            reset_animation_preview();
            return;
        }
        auto entity = m_selection.get_selected_entity();
        const auto& controller = m_asset_inspector.controller_editor();
        if(m_asset_inspector.controller_active() && controller.is_open() && controller.model())
            entity = m_selection.get_scene().find_entity(controller.model()->entity);
        if(!bind_animation_preview(entity))
            return;
        std::optional<Comet::AnimationRootMotion> root_motion;
        if(entity.has_component<Comet::RootMotionComponent>()) {
            const auto& root = entity.get_component<Comet::RootMotionComponent>();
            if(root.enabled)
                root_motion = {root.node, root.horizontal, root.vertical, root.rotation};
        }
        m_animation_preview.set_root_motion(std::move(root_motion));
        m_animation_preview.advance(delta_time);
        m_animation_preview.publish_to(m_selection.get_scene());
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
        const Comet::ComponentDescriptor& component,
        const Comet::PropertyDescriptor& source_property) {
        auto property = source_property;
        if(!property.editable || property.read_only)
            return;
        const bool audio_distance =
            component.id == "audio_source"
            && (property.id == "min_distance" || property.id == "max_distance"
                || property.id == "rolloff");
        const bool disabled =
            audio_distance && std::ranges::all_of(m_entities, [&](Comet::EntityUuid uuid) {
                return !m_selection.get_scene()
                            .find_entity(uuid)
                            .get_component<Comet::AudioSourceComponent>()
                            .spatial;
            });
        if(disabled)
            ImGui::BeginDisabled();
        const Comet::ScopeExit enable([&] {
            if(disabled)
                ImGui::EndDisabled();
        });
        auto value = property.copy_value(component.get_component(std::as_const(entity)));
        if(!value)
            return;
        const bool mixed = std::ranges::any_of(m_entities, [&](auto uuid) {
            return property.copy_value(
                       component.get_component(m_selection.get_scene().find_entity(uuid)))
                   != value;
        });
        if(mixed)
            property.display_name = mixed_label(property.display_name);
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
        const Comet::ScopeExit pop_mixed([] { ImGui::PopItemFlag(); });
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
        if((component.id == "bone_attachment" && property.id == "bone")
            || (component.id == "root_motion" && property.id == "node")
            || (component.id == "foot_placement"
                && (property.id == "left_foot" || property.id == "right_foot"
                    || property.id == "pelvis"))) {
            const bool root_motion = component.id == "root_motion";
            const bool foot_placement = component.id == "foot_placement";
            const bool pelvis = foot_placement && property.id == "pelvis";
            const bool own_skeleton = root_motion || foot_placement;
            auto parent = own_skeleton ? entity : m_selection.get_scene().get_parent(entity);
            std::shared_ptr<const Comet::AnimationData> data;
            if(parent && parent.has_component<Comet::MeshRendererComponent>()) {
                const auto mesh = m_runtime_assets.resolve<Comet::Mesh>(
                    parent.get_component<Comet::MeshRendererComponent>().mesh);
                if(mesh)
                    data = mesh->animation_data();
            }
            auto& name = std::get<std::string>(*value);
            if(data
                && ImGui::BeginCombo(
                    property.display_name.c_str(), name.empty() ? "选择骨骼" : name.c_str())) {
                if(root_motion && ImGui::Selectable("自动选择骨架根", !mixed && name.empty())) {
                    name.clear();
                    result.changed = true;
                }
                if(foot_placement
                    && ImGui::Selectable(
                        pelvis ? "不使用骨盆补偿" : "不使用此脚", !mixed && name.empty())) {
                    name.clear();
                    result.changed = true;
                }
                for(const auto& joint : data->joints) {
                    if(foot_placement && !pelvis) {
                        const auto middle = data->nodes[joint.node].parent;
                        if(middle >= data->nodes.size()
                            || data->nodes[middle].parent >= data->nodes.size())
                            continue;
                    }
                    const auto& bone = data->nodes[joint.node].name;
                    if(ImGui::Selectable(bone.c_str(), !mixed && bone == name)) {
                        name = bone;
                        result.changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            if(!data) {
                if(own_skeleton)
                    ImGui::TextWrapped("请先为此实体设置带骨骼的网格与动画。");
                else
                    ImGui::TextWrapped("请先在层级中将此物体拖到角色模型下，再选择骨骼。");
            } else if(!name.empty() && std::ranges::none_of(data->joints, [&](const auto& joint) {
                          return data->nodes[joint.node].name == name;
                      }))
                ImGui::TextColored({1, .45f, .35f, 1}, "骨骼已丢失：%s", name.c_str());
            if(root_motion)
                ImGui::TextWrapped(
                    "选择包含全部关节的根骨骼；目标须为自身或祖先。角色位移参与碰撞求解；编辑预览保持原地。");
            else if(pelvis)
                ImGui::TextWrapped(
                    "选择两条腿链的共同祖先；权重大于零时下降骨盆以减轻腿部过伸，只改变视觉姿态。");
            else if(foot_placement)
                ImGui::TextWrapped(
                    "选择踝关节，其父／祖父为膝盖／大腿；至少配置一只脚。Play／App 中按物理地面修正，抬脚与离地时释放。");
            else
                ImGui::TextWrapped("本地变换是骨骼空间的偏移；暂停时保留姿态，Stop 恢复绑定姿态。");
            result.finished = result.changed;
            apply_property_edit(entity, component, property, *value, result);
            return;
        }
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
                    if(ImGui::Selectable("绑定姿态", clip.empty() && !mixed)
                        && (!clip.empty() || mixed)) {
                        clip.clear();
                        result.changed = true;
                    }
                    for(const auto& candidate : clips) {
                        const bool available =
                            std::ranges::all_of(m_entities, [&](Comet::EntityUuid uuid) {
                                const auto other = m_selection.get_scene().find_entity(uuid);
                                if(!other.has_component<Comet::MeshRendererComponent>())
                                    return false;
                                const auto source = m_runtime_assets.resolve<Comet::Mesh>(
                                    other.get_component<Comet::MeshRendererComponent>().mesh);
                                return source && source->animation_data()
                                       && source->animation_data()->find_clip(candidate.name);
                            });
                        if(available
                            && ImGui::Selectable(
                                candidate.name.c_str(), !mixed && candidate.name == clip)
                            && (mixed || candidate.name != clip)) {
                            clip = candidate.name;
                            result.changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                result.finished = result.changed;
                apply_property_edit(entity, component, property, *value, result);
                return;
            }
        }
        std::optional<std::uint64_t> drop_generation;
        if(m_state.mode == EditorMode::Edit && m_history.get_scene() == &m_selection.get_scene())
            drop_generation = m_history.generation();
        if(property.type == Comet::PropertyType::EntityReference) {
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
                    return m_property_editor_registry.edit_property(
                        property, &edited, &m_selection.get_scene(), drop_generation);
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
            std::vector<Comet::PropertyValue> aligned;
            for(auto uuid : m_entities) {
                const auto value = aligned_joint_value(m_selection.get_scene(),
                    m_selection.get_scene().find_entity(uuid), component,
                    std::string_view(property.id).substr(std::string_view("connected_").size()));
                if(!value)
                    break;
                aligned.emplace_back(*value);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(aligned.size() != m_entities.size());
            if(ImGui::Button("对齐###Align joint")) {
                if(m_property_edit.commit())
                    apply_property_values(property_targets(component.id, property.id), aligned,
                        {.changed = true, .finished = true});
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

    void InspectorPanel::render_animation_parameters(Comet::Entity entity,
        const Comet::ComponentDescriptor& component, const Comet::PropertyDescriptor& property) {
        if(m_entities.size() != 1) {
            ImGui::TextDisabled("动画参数请单独编辑");
            return;
        }
        const auto& animator = entity.get_component<Comet::AnimatorComponent>();
        const auto controller =
            m_runtime_assets.resolve<Comet::AnimationController>(animator.controller);
        if(!controller) {
            ImGui::TextDisabled("动画控制器未加载，请查看日志");
            return;
        }
        if(entity.has_component<Comet::CharacterAnimationComponent>())
            ImGui::TextWrapped(
                "运行时角色驱动提供 speed、grounded、rising、crouched；动作和阈值在控制器资产中编辑。");
        if(ImGui::Button("恢复默认参数###Restore animation parameters")) {
            apply_property_edit(entity, component, property, Comet::ParameterMap{},
                {.changed = true, .finished = true});
            return;
        }
        if(auto valid = controller->validate_parameters(animator.parameters); !valid) {
            ImGui::TextWrapped("%s", valid.error().c_str());
            return;
        }
        for(const auto& [name, initial] : controller->parameters) {
            auto value = *controller->parameter(name, animator.parameters);
            bool changed = false;
            if(auto* boolean = std::get_if<bool>(&value))
                changed = ImGui::Checkbox(name.c_str(), boolean);
            else if(auto* number = std::get_if<float>(&value))
                changed = ImGui::DragFloat(name.c_str(), number, .05f);
            const auto result = PropertyEditResult::from_item(changed);
            auto parameters = animator.parameters;
            if(changed) {
                if(value == initial)
                    parameters.erase(name);
                else
                    parameters[name] = value;
            }
            apply_property_edit(entity, component, property, parameters, result);
        }
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
        const auto targets = property_targets(component.id, property.id);
        if(m_script_edit_version != script) {
            if(m_property_edit.targets_many(targets) && !m_property_edit.cancel()) {
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
        std::vector<const Comet::ParameterMap*> peers;
        for(auto uuid : m_entities) {
            const auto other = m_selection.get_scene().find_entity(uuid);
            if(other.get_component<Comet::ScriptComponent>().asset != binding.asset) {
                ImGui::TextDisabled("不同脚本的参数请单独编辑");
                return;
            }
            if(m_state.mode == EditorMode::Play && m_runtime) {
                const auto* scripts = m_runtime->find_system<Comet::ScriptSystem>();
                if(!scripts || scripts->running_script(other) != script) {
                    ImGui::TextDisabled("运行中的脚本版本不同，请单独编辑参数");
                    return;
                }
            }
            peers.push_back(&other.get_component<Comet::ScriptComponent>().parameters);
        }
        if(ImGui::Button("恢复默认参数###Restore default parameters")) {
            const std::vector<Comet::PropertyValue> values(targets.size(), Comet::ParameterMap{});
            apply_property_values(targets, values, {.changed = true, .finished = true});
            return;
        }
        bool compatible = true;
        bool repairable = true;
        bool mixed = false;
        std::vector<Comet::PropertyValue> repaired;
        for(auto uuid : m_entities) {
            const auto other = m_selection.get_scene().find_entity(uuid);
            const auto& parameters = other.get_component<Comet::ScriptComponent>().parameters;
            mixed |= parameters != binding.parameters;
            if(const auto checked = script->validate_overrides(parameters); !checked) {
                ImGui::TextWrapped("%s: %s",
                    other.get_component<Comet::NameComponent>().name.c_str(),
                    checked.error().message.c_str());
                compatible = false;
            }
            auto value = parameters;
            script->retain_compatible_overrides(value);
            repairable &= bool(script->validate_overrides(value));
            repaired.emplace_back(std::move(value));
        }
        if(!compatible) {
            if(repairable && ImGui::Button("移除不兼容参数覆盖###Remove incompatible overrides"))
                apply_property_values(targets, repaired, {.changed = true, .finished = true});
            return;
        }
        if(mixed)
            ImGui::TextDisabled("参数包含多值；仅同步修改的参数，其他覆盖保持各自配置");
        auto overrides = binding.parameters;
        std::optional<std::uint64_t> drop_generation;
        if(m_state.mode == EditorMode::Edit && m_history.get_scene() == &m_selection.get_scene())
            drop_generation = m_history.generation();
        const auto result = m_property_editor_registry.edit_parameters(
            script->properties(), overrides, m_selection.get_scene(), drop_generation, peers);
        std::vector<Comet::PropertyValue> values;
        if(result.changed)
            for(const auto* parameters : peers)
                values.emplace_back(merge_parameters(*script, binding.parameters, overrides,
                    *parameters, result.changed_parameters));
        apply_property_values(targets, values, result);
        m_script_active_item = result.active ? m_active_item : 0;
    }

    std::vector<PropertyEditTransaction::Target> InspectorPanel::property_targets(
        std::string_view component, std::string_view property) const {
        std::vector<PropertyEditTransaction::Target> targets;
        targets.reserve(m_entities.size());
        for(auto uuid : m_entities)
            targets.push_back({uuid, std::string(component), std::string(property)});
        return targets;
    }

    void InspectorPanel::apply_property_edit(Comet::Entity entity,
        const Comet::ComponentDescriptor& component, const Comet::PropertyDescriptor& property,
        const Comet::PropertyValue& value, const PropertyEditResult& result) {
        const auto targets = property_targets(component.id, property.id);
        std::vector<Comet::PropertyValue> values;
        if(result.changed) {
            const auto before = property.copy_value(component.get_component(entity));
            if(!before)
                return;
            for(const auto& target : targets) {
                const auto current = property.copy_value(
                    component.get_component(m_selection.get_scene().find_entity(target.entity)));
                if(!current)
                    return;
                values.push_back(merge_edited_value(*before, value, *current));
            }
        }
        apply_property_values(targets, values, result);
    }

    void InspectorPanel::apply_property_values(
        std::span<const PropertyEditTransaction::Target> targets,
        std::span<const Comet::PropertyValue> values, const PropertyEditResult& result) {
        if(result.active) {
            m_active_item = result.active_item;
            if(!m_active_item)
                m_active_item = ImGui::GetItemID();
        }
        if(m_state.mode == EditorMode::Play) {
            if(result.changed
                && !m_property_edit.write_many(m_selection.get_scene(), targets, values))
                LOG_ERROR("Cannot update runtime properties");
            return;
        }
        if((result.began || result.changed) && !m_property_edit.targets_many(targets)) {
            if(!m_property_edit.begin_many(targets)) {
                LOG_ERROR("Cannot begin property edit");
                return;
            }
        }
        if(!m_property_edit.targets_many(targets))
            return;
        if(ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if(!m_property_edit.cancel())
                LOG_ERROR("Cannot cancel property edit");
            ImGui::ClearActiveID();
            return;
        }
        if(result.changed && !m_property_edit.preview_many(values))
            LOG_ERROR("Cannot preview property edit");
        if((result.finished || (result.changed && !result.active)) && !m_property_edit.commit())
            LOG_ERROR("Cannot commit property edit");
    }

    std::optional<InspectorPanel::AssetAssignment> InspectorPanel::take_asset_assignment() {
        return std::exchange(m_asset_assignment, std::nullopt);
    }

    std::optional<InspectorPanel::BakeRequest> InspectorPanel::take_bake_request() {
        return std::exchange(m_bake_request, std::nullopt);
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
                AssetAssignment{property_targets(target.component, target.property),
                    {{selected, m_asset_database.get_revision(selected)}, m_history.generation(),
                        type}};
        if(const auto asset = accept_asset_drop(type);
            asset
            && (asset->target.handle != handle
                || (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_MixedValue)))
            m_asset_assignment =
                AssetAssignment{property_targets(target.component, target.property), *asset};
    }

}

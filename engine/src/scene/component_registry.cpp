#include "scene/component_registry.h"
#include <unordered_set>

#include "scene/components.h"
#include "scene/prefab.h"
#include "scene/script_component.h"

#include "diagnostics/logger.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace Comet {
    namespace {
        template<typename Joint> std::vector<PropertyDescriptor> joint_properties() {
            auto properties = std::vector<PropertyDescriptor>{
                make_property_descriptor("enabled", "Enabled", &Joint::enabled),
                make_property_descriptor(
                    "connected_body", "Connected Body", &Joint::connected_body),
                make_property_descriptor("anchor", "Local Anchor", &Joint::anchor),
                make_property_descriptor(
                    "connected_anchor", "Connected Anchor", &Joint::connected_anchor),
                make_property_descriptor("break_force", "Break Force (N)", &Joint::break_force,
                    {.required = false,
                        .numeric = {.speed = 1, .minimum = 0, .enforce_bounds = true}})};
            if constexpr(!std::is_same_v<Joint, PointJointComponent>)
                properties.push_back(make_property_descriptor("break_torque", "Break Torque (N.m)",
                    &Joint::break_torque,
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}}));
            return properties;
        }

        template<typename Joint> std::vector<PropertyDescriptor> axis_joint_properties() {
            auto properties = joint_properties<Joint>();
            properties.push_back(make_property_descriptor("axis", "Local Axis", &Joint::axis));
            properties.push_back(make_property_descriptor(
                "connected_axis", "Connected Axis", &Joint::connected_axis));
            return properties;
        }

        template<typename Joint>
        void append_joint_controls(std::vector<PropertyDescriptor>& properties,
            PropertyDescriptor target, PropertyDescriptor speed, PropertyDescriptor capacity) {
            properties.insert(properties.end(),
                {make_property_descriptor("limits_frequency", "Limit Frequency (Hz)",
                     &Joint::limits_frequency,
                     {.required = false,
                         .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}}),
                    make_property_descriptor("limits_damping", "Limit Damping Ratio",
                        &Joint::limits_damping,
                        {.required = false,
                            .numeric = {.speed = 0.05f, .minimum = 0, .enforce_bounds = true}}),
                    make_property_descriptor("motor_enabled", "Motor Enabled",
                        &Joint::motor_enabled, {.required = false}),
                    make_enum_property_descriptor<Joint, JointMotorMode>("motor_mode", "Motor Mode",
                        &Joint::motor_mode,
                        {{JointMotorMode::Velocity, {"velocity", "Velocity"}},
                            {JointMotorMode::Position, {"position", "Position"}}},
                        {.required = false}),
                    std::move(target), std::move(speed), std::move(capacity),
                    make_property_descriptor("motor_frequency", "Motor Frequency (Hz)",
                        &Joint::motor_frequency,
                        {.required = false,
                            .numeric = {.speed = 0.1f, .minimum = 0.001f, .enforce_bounds = true}}),
                    make_property_descriptor("motor_damping", "Motor Damping Ratio",
                        &Joint::motor_damping,
                        {.required = false,
                            .numeric = {.speed = 0.05f, .minimum = 0, .enforce_bounds = true}})});
        }
    }

    bool ComponentDescriptor::assign_property(Entity entity, std::string_view property_id,
        const PropertyValue& value, PropertyDescriptor::WriteMode mode) const {
        const auto* property = find_property(property_id);
        if(!has_component(entity) || !property || !assign_property_callback)
            return false;
        return assign_property_callback(entity, *property, value, mode);
    }

    std::any ComponentDescriptor::capture_component(const Entity& entity) const {
        if(!entity || !has_component(entity) || !capture_component_callback)
            return {};
        return capture_component_callback(entity);
    }

    bool ComponentDescriptor::restore_component(Entity& entity, const std::any& snapshot) const {
        if(!entity || has_component(entity) || !snapshot.has_value() || !restore_component_callback)
            return false;
        return restore_component_callback(entity, snapshot);
    }

    bool ComponentRegistry::register_component(ComponentDescriptor descriptor) {
        if(descriptor.id.empty() || descriptor.display_name.empty()
            || !descriptor.has_component_callback || !descriptor.assign_property_callback
            || !descriptor.const_component_accessor || find_component(descriptor.id) != nullptr) {
            return false;
        }

        for(const auto& existing : m_components)
            if(descriptor.type_id != 0 && existing.type_id == descriptor.type_id)
                return false;

        std::unordered_set<std::string> property_ids;
        for(const PropertyDescriptor& property : descriptor.properties) {
            if(property.id.empty() || property.display_name.empty() || !property.mutable_accessor
                || !property.const_accessor || (property.transient && property.serializable)
                || (property.numeric.enforce_bounds
                    && (!property.numeric.minimum && !property.numeric.maximum))
                || (property.numeric.minimum && !std::isfinite(*property.numeric.minimum))
                || (property.numeric.maximum && !std::isfinite(*property.numeric.maximum))
                || (property.numeric.minimum && property.numeric.maximum
                    && *property.numeric.minimum > *property.numeric.maximum)
                || (property.asset_type
                    && (property.type != PropertyType::AssetHandle
                        || *property.asset_type == AssetType::Unknown))
                || (property.asset_cpu_only && property.asset_type != AssetType::Mesh)
                || !property_ids.insert(property.id).second) {
                return false;
            }
            if(property.type == PropertyType::Enum) {
                if(property.enum_options.empty() || !property.read_enum || !property.write_enum)
                    return false;
                std::unordered_set<std::string> names;
                for(const auto& option : property.enum_options)
                    if(option.id.empty() || option.display_name.empty()
                        || !names.insert(option.id).second)
                        return false;
            } else if(!property.enum_options.empty() || property.read_enum || property.write_enum) {
                return false;
            }
        }

        m_components.push_back(std::move(descriptor));
        return true;
    }

    const ComponentDescriptor* ComponentRegistry::find_component(
        const std::string_view component_id) const {
        for(const ComponentDescriptor& component : m_components) {
            if(component.id == component_id) {
                return &component;
            }
        }
        return nullptr;
    }

    bool ComponentRegistry::covers_entity(const Entity& entity) const {
        if(!entity)
            return false;
        for(auto&& [type, storage] : entity.m_scene->m_registry.storage()) {
            if(!storage.contains(entity.m_handle))
                continue;
            if(type == entt::type_hash<IdComponent>::value()
                || type == entt::type_hash<UuidComponent>::value()
                || type == entt::type_hash<NameComponent>::value()
                || type == entt::type_hash<RelationshipComponent>::value()
                || type == entt::type_hash<WorldTransformComponent>::value()
                || type == entt::type_hash<PrefabInstanceComponent>::value())
                continue;
            bool registered = false;
            for(const auto& component : m_components)
                registered |= component.type_id == type;
            if(!registered)
                return false;
        }
        return true;
    }

    std::vector<AssetReference> ComponentRegistry::collect_asset_references(
        Scene& scene, const ReferenceScope scope) const {
        std::vector<AssetReference> references;
        if(scope != ReferenceScope::All && scene.get_particles()) {
            for(const auto& batch : scene.get_particles()->batches)
                if(batch.texture)
                    references.push_back({batch.texture, AssetType::Texture, true});
            for(const auto& emitter : scene.get_particles()->gpu_emitters) {
                if(emitter.settings.texture)
                    references.push_back({emitter.settings.texture, AssetType::Texture, true});
                if(emitter.settings.vector_field)
                    references.push_back(
                        {emitter.settings.vector_field, AssetType::VectorField, true});
            }
        }
        const auto& environment = scene.get_environment();
        if(environment.asset
            && (scope == ReferenceScope::All || environment.background || environment.lighting))
            references.push_back({environment.asset, AssetType::Environment, false});
        for(const auto entity : scene.get_entities()) {
            if(scope == ReferenceScope::All && entity.has_component<PrefabInstanceComponent>()) {
                const auto& instance = entity.get_component<PrefabInstanceComponent>();
                references.push_back({instance.asset, instance.source_type, false});
            }
            for(const auto& component : m_components) {
                if(scope == ReferenceScope::Resident && component.id == "stream_cell")
                    continue;
                const auto* data = component.get_component(entity);
                if(!data)
                    continue;
                if(scope != ReferenceScope::All && component.id == "collider"
                    && !entity.get_component<ColliderComponent>().uses_mesh())
                    continue;
                if(scope != ReferenceScope::All && component.id == "reflection_probe"
                    && !entity.get_component<ReflectionProbeComponent>().enabled)
                    continue;
                if(scope != ReferenceScope::All && component.id == "irradiance_volume"
                    && (!entity.get_component<IrradianceVolumeComponent>().enabled
                        || entity.get_component<IrradianceVolumeComponent>().realtime))
                    continue;
                if(scope != ReferenceScope::All && component.id == "decal"
                    && !entity.get_component<DecalComponent>().enabled)
                    continue;
                for(const auto& property : component.properties) {
                    if(property.type != PropertyType::AssetHandle || !property.asset_type)
                        continue;
                    if(const auto value = property.copy_value(data)) {
                        if(const auto handle = std::get<AssetHandle>(*value))
                            references.push_back(
                                {handle, *property.asset_type, true, property.asset_cpu_only});
                    }
                }
            }
        }
        std::ranges::sort(references);
        references.erase(std::unique(references.begin(), references.end()), references.end());
        return references;
    }

    ComponentRegistry create_scene_component_registry() {
        ComponentRegistry registry;
        const auto register_component = [&registry](ComponentDescriptor descriptor) {
            if(!registry.register_component(std::move(descriptor))) {
                LOG_FATAL("Invalid built-in component descriptor");
            }
        };

        register_component(make_component_descriptor<MeshLodComponent>("mesh_lod", "静态网格 LOD",
            {make_property_descriptor("level1", "LOD 1 网格", &MeshLodComponent::level1,
                 {.required = false, .asset_type = AssetType::Mesh}),
                make_property_descriptor("level2", "LOD 2 网格", &MeshLodComponent::level2,
                    {.required = false, .asset_type = AssetType::Mesh}),
                make_property_descriptor("level1_height", "LOD 1 屏幕高度比例",
                    &MeshLodComponent::level1_height,
                    {.required = false,
                        .numeric = {.speed = 0.01f,
                            .minimum = 0.001f,
                            .maximum = 1,
                            .enforce_bounds = true}}),
                make_property_descriptor("level2_height", "LOD 2 屏幕高度比例",
                    &MeshLodComponent::level2_height,
                    {.required = false,
                        .numeric = {.speed = 0.01f,
                            .minimum = 0.001f,
                            .maximum = 1,
                            .enforce_bounds = true}}),
                make_property_descriptor("hysteresis", "切换滞回比例",
                    &MeshLodComponent::hysteresis,
                    {.required = false,
                        .numeric = {.speed = 0.01f,
                            .minimum = 0,
                            .maximum = 0.5f,
                            .enforce_bounds = true}}),
                make_enum_property_descriptor<MeshLodComponent, MeshLod::Preview>("preview",
                    "层级预览", &MeshLodComponent::preview,
                    {{MeshLod::Preview::Auto, {"auto", "自动"}},
                        {MeshLod::Preview::Original, {"original", "LOD 0（原网格）"}},
                        {MeshLod::Preview::Level1, {"level1", "LOD 1"}},
                        {MeshLod::Preview::Level2, {"level2", "LOD 2"}}},
                    {.required = false})}));

        register_component(make_component_descriptor<NavigationRegionComponent>("navigation_region",
            "导航区域",
            {make_property_descriptor("data", "导航产品", &NavigationRegionComponent::data,
                 {.required = false, .asset_type = AssetType::Navigation}),
                make_property_descriptor("half_extents", "半尺寸（米）",
                    &NavigationRegionComponent::half_extents,
                    {.required = false,
                        .numeric = {.speed = 0.05f,
                            .minimum = 0.1,
                            .maximum = 1000,
                            .enforce_bounds = true}}),
                make_property_descriptor("cell_size", "网格精度（米）",
                    &NavigationRegionComponent::cell_size,
                    {.required = false,
                        .numeric = {.speed = 0.05f,
                            .minimum = 0.05,
                            .maximum = 2,
                            .enforce_bounds = true}}),
                make_property_descriptor("agent_radius", "角色半径（米）",
                    &NavigationRegionComponent::agent_radius,
                    {.required = false,
                        .numeric = {.speed = 0.05f,
                            .minimum = 0.05,
                            .maximum = 10,
                            .enforce_bounds = true}}),
                make_property_descriptor("agent_height", "角色高度（米）",
                    &NavigationRegionComponent::agent_height,
                    {.required = false,
                        .numeric = {.speed = 0.05f,
                            .minimum = 0.1,
                            .maximum = 20,
                            .enforce_bounds = true}}),
                make_property_descriptor("max_climb", "台阶高度（米）",
                    &NavigationRegionComponent::max_climb,
                    {.required = false,
                        .numeric =
                            {.speed = 0.05f, .minimum = 0, .maximum = 2, .enforce_bounds = true}}),
                make_property_descriptor("max_slope", "最大坡度（度）",
                    &NavigationRegionComponent::max_slope,
                    {.required = false,
                        .numeric = {.speed = 0.05f,
                            .minimum = 0,
                            .maximum = 89,
                            .enforce_bounds = true}})}));

        register_component(make_component_descriptor<NavigationObstacleComponent>(
            "navigation_obstacle", "动态导航障碍",
            {make_property_descriptor(
                 "enabled", "启用阻塞", &NavigationObstacleComponent::enabled, {.required = false}),
                make_property_descriptor("half_extents", "半尺寸（本地米）",
                    &NavigationObstacleComponent::half_extents,
                    {.required = false,
                        .numeric = {.speed = .05f,
                            .minimum = .01f,
                            .maximum = 1000,
                            .enforce_bounds = true}})}));
        register_component(make_component_descriptor<NavigationLinkComponent>("navigation_link",
            "导航连接（跳跃）",
            {make_property_descriptor(
                 "end", "终点（本地坐标）", &NavigationLinkComponent::end, {.required = false}),
                make_property_descriptor("radius", "起降半径（米）",
                    &NavigationLinkComponent::radius,
                    {.required = false,
                        .numeric = {.speed = .05f,
                            .minimum = .05f,
                            .maximum = 10,
                            .enforce_bounds = true}}),
                make_property_descriptor("bidirectional", "允许往返",
                    &NavigationLinkComponent::bidirectional, {.required = false})}));

        register_component(
            make_component_descriptor<NavigationAgentComponent>("navigation_agent", "导航角色",
                {make_property_descriptor(
                     "enabled", "启用", &NavigationAgentComponent::enabled, {.required = false}),
                    make_property_descriptor("facing", "朝向实体",
                        &NavigationAgentComponent::facing, {.required = false}),
                    make_property_descriptor("arrival_distance", "到达距离（米）",
                        &NavigationAgentComponent::arrival_distance,
                        {.required = false,
                            .numeric = {.speed = 0.05f,
                                .minimum = 0.05,
                                .maximum = 5,
                                .enforce_bounds = true}}),
                    make_property_descriptor("repath_interval", "重寻路间隔（秒）",
                        &NavigationAgentComponent::repath_interval,
                        {.required = false,
                            .numeric = {.speed = 0.05f,
                                .minimum = 0.05,
                                .maximum = 10,
                                .enforce_bounds = true}})}));

        register_component(make_component_descriptor<ScriptComponent>("script", "Script",
            {make_property_descriptor(
                 "asset", "Script", &ScriptComponent::asset, {.asset_type = AssetType::Script}),
                make_property_descriptor(
                    "parameters", "Parameters", &ScriptComponent::parameters)}));

        // 名称已由 .scene 单独保存，避免重复序列化。
        register_component(make_component_descriptor<NameComponent>("name", "Name",
            {make_property_descriptor("name", "Name", &NameComponent::name)}, false));

        register_component(make_component_descriptor<TransformComponent>("transform", "Transform",
            {make_property_descriptor(
                 "translation", "Translation", &TransformComponent::translation),
                make_property_descriptor("rotation", "Rotation", &TransformComponent::rotation,
                    {.numeric = {.speed = 1.0f}},
                    [](Math::Vec3& rotation) { rotation = Math::wrap_degrees(rotation); }),
                make_property_descriptor("scale", "Scale", &TransformComponent::scale)}));

        register_component(
            make_component_descriptor<MeshRendererComponent>("mesh_renderer", "Mesh Renderer",
                {make_property_descriptor(
                     "mesh", "Mesh", &MeshRendererComponent::mesh, {.asset_type = AssetType::Mesh}),
                    make_property_descriptor("material", "Material",
                        &MeshRendererComponent::material, {.asset_type = AssetType::Material}),
                    make_property_descriptor("contributes_to_gi", "参与 GI 几何",
                        &MeshRendererComponent::contributes_to_gi, {.required = false})}));

        register_component(make_component_descriptor<OutlineComponent>("outline", "描边／遮罩",
            {make_property_descriptor("enabled", "启用", &OutlineComponent::enabled),
                make_property_descriptor("color", "颜色", &OutlineComponent::color),
                make_property_descriptor("width", "宽度（输出像素）", &OutlineComponent::width,
                    {.numeric =
                            {.speed = .1f, .minimum = 0, .maximum = 8, .enforce_bounds = true}}),
                make_property_descriptor("fill", "填充透明度", &OutlineComponent::fill,
                    {.numeric =
                            {.speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}}),
                make_property_descriptor(
                    "through_walls", "穿透遮挡", &OutlineComponent::through_walls)}));

        register_component(make_component_descriptor<AnimatorComponent>("animator", "Animator",
            {make_property_descriptor(
                 "enabled", "enabled", &AnimatorComponent::enabled, {.required = false}),
                make_property_descriptor(
                    "clip", "clip", &AnimatorComponent::clip, {.required = false}),
                make_property_descriptor("speed", "speed", &AnimatorComponent::speed,
                    {.required = false,
                        .numeric = {.speed = 0.05f, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor(
                    "loop", "loop", &AnimatorComponent::loop, {.required = false}),
                make_property_descriptor("transition", "transition", &AnimatorComponent::transition,
                    {.required = false,
                        .numeric = {.speed = 0.01f, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor("controller", "Controller", &AnimatorComponent::controller,
                    {.required = false, .asset_type = AssetType::AnimationController}),
                make_property_descriptor("parameters", "Parameters", &AnimatorComponent::parameters,
                    {.required = false})}));
        register_component(make_component_descriptor<RootMotionComponent>("root_motion", "根运动",
            {make_property_descriptor("enabled", "启用", &RootMotionComponent::enabled),
                make_property_descriptor("node", "根骨骼（留空自动）", &RootMotionComponent::node),
                make_property_descriptor(
                    "target", "移动目标（留空自身）", &RootMotionComponent::target),
                make_property_descriptor(
                    "horizontal", "水平位移", &RootMotionComponent::horizontal),
                make_property_descriptor("vertical", "垂直位移", &RootMotionComponent::vertical),
                make_property_descriptor("rotation", "根旋转", &RootMotionComponent::rotation)}));
        register_component(make_component_descriptor<HandIKComponent>("hand_ik", "手部 IK",
            {make_property_descriptor("enabled", "启用", &HandIKComponent::enabled),
                make_property_descriptor("left_hand", "左手骨骼", &HandIKComponent::left_hand),
                make_property_descriptor("right_hand", "右手骨骼", &HandIKComponent::right_hand),
                make_property_descriptor("left_target", "左手目标", &HandIKComponent::left_target),
                make_property_descriptor(
                    "right_target", "右手目标", &HandIKComponent::right_target),
                make_property_descriptor(
                    "left_pole", "左肘提示点（模型空间）", &HandIKComponent::left_pole),
                make_property_descriptor(
                    "right_pole", "右肘提示点（模型空间）", &HandIKComponent::right_pole),
                make_property_descriptor("weight", "权重", &HandIKComponent::weight,
                    {.numeric = {.minimum = 0, .maximum = 1, .enforce_bounds = true}}),
                make_property_descriptor(
                    "match_rotation", "匹配目标朝向", &HandIKComponent::match_rotation)}));
        register_component(make_component_descriptor<FullBodyIKComponent>("full_body_ik", "全身 IK",
            {make_property_descriptor("enabled", "启用", &FullBodyIKComponent::enabled),
                make_property_descriptor("root", "骨盆／躯干根骨骼", &FullBodyIKComponent::root),
                make_property_descriptor("left_foot", "左脚骨骼", &FullBodyIKComponent::left_foot),
                make_property_descriptor(
                    "right_foot", "右脚骨骼", &FullBodyIKComponent::right_foot),
                make_property_descriptor(
                    "left_target", "左脚目标（空时沿用贴地）", &FullBodyIKComponent::left_target),
                make_property_descriptor(
                    "right_target", "右脚目标（空时沿用贴地）", &FullBodyIKComponent::right_target),
                make_property_descriptor(
                    "left_pole", "左膝提示点", &FullBodyIKComponent::left_pole),
                make_property_descriptor(
                    "right_pole", "右膝提示点", &FullBodyIKComponent::right_pole),
                make_property_descriptor("weight", "身体跟随权重", &FullBodyIKComponent::weight,
                    {.numeric = {.minimum = 0, .maximum = 1, .enforce_bounds = true}}),
                make_property_descriptor("max_translation", "骨盆最大位移（米）",
                    &FullBodyIKComponent::max_translation,
                    {.numeric = {.minimum = 0, .maximum = 2, .enforce_bounds = true}}),
                make_property_descriptor("max_rotation", "躯干最大转角（度）",
                    &FullBodyIKComponent::max_rotation,
                    {.numeric = {.minimum = 0, .maximum = 60, .enforce_bounds = true}}),
                make_property_descriptor("iterations", "求解迭代数",
                    &FullBodyIKComponent::iterations,
                    {.numeric = {.minimum = 1, .maximum = 12, .enforce_bounds = true}})}));
        register_component(make_component_descriptor<StreamCellComponent>("stream_cell",
            "流式世界单元",
            {make_property_descriptor(
                 "enabled", "启用", &StreamCellComponent::enabled, {.required = false}),
                make_property_descriptor("prefab", "单元 Prefab", &StreamCellComponent::prefab,
                    {.asset_type = AssetType::Prefab}),
                make_property_descriptor("load_distance", "加载距离",
                    &StreamCellComponent::load_distance,
                    {.required = false,
                        .numeric = {.minimum = 1, .maximum = 100000, .enforce_bounds = true}}),
                make_property_descriptor("unload_distance", "卸载距离",
                    &StreamCellComponent::unload_distance,
                    {.required = false,
                        .numeric = {.minimum = 1, .maximum = 100000, .enforce_bounds = true}})}));
        register_component(make_component_descriptor<SplineComponent>("spline", "三次样条",
            {make_property_descriptor(
                 "start", "起点", &SplineComponent::start, {.required = false}),
                make_property_descriptor(
                    "control_a", "控制点 A", &SplineComponent::control_a, {.required = false}),
                make_property_descriptor(
                    "control_b", "控制点 B", &SplineComponent::control_b, {.required = false}),
                make_property_descriptor(
                    "end", "终点", &SplineComponent::end, {.required = false})}));

        register_component(
            make_component_descriptor<SplineFollowerComponent>("spline_follower", "样条跟随／过场",
                {make_property_descriptor(
                     "path", "路径实体", &SplineFollowerComponent::path, {.required = false}),
                    make_property_descriptor(
                        "playing", "播放", &SplineFollowerComponent::playing, {.required = false}),
                    make_property_descriptor("duration", "时长（秒）",
                        &SplineFollowerComponent::duration,
                        {.required = false,
                            .numeric =
                                {.minimum = 0.01f, .maximum = 86400.0f, .enforce_bounds = true}}),
                    make_property_descriptor(
                        "loop", "循环", &SplineFollowerComponent::loop, {.required = false}),
                    make_property_descriptor("orient", "沿路径转向",
                        &SplineFollowerComponent::orient, {.required = false}),
                    make_property_descriptor(
                        "ease", "缓入缓出", &SplineFollowerComponent::ease, {.required = false}),
                    make_property_descriptor("skip_action", "跳过输入动作",
                        &SplineFollowerComponent::skip_action, {.required = false}),
                    make_property_descriptor("finished_event", "完成通知",
                        &SplineFollowerComponent::finished_event, {.required = false})}));

        register_component(make_component_descriptor<WeatherComponent>("weather", "天气与日照",
            {make_property_descriptor(
                 "enabled", "启用", &WeatherComponent::enabled, {.required = false}),
                make_property_descriptor("hour", "时刻（小时）", &WeatherComponent::hour,
                    {.required = false,
                        .numeric = {.minimum = 0.0f, .maximum = 24.0f, .enforce_bounds = true}}),
                make_property_descriptor("day_length", "昼夜周期（秒；0 固定）",
                    &WeatherComponent::day_length,
                    {.required = false,
                        .numeric = {.minimum = 0.0f, .maximum = 86400.0f, .enforce_bounds = true}}),
                make_property_descriptor("overcast", "云量", &WeatherComponent::overcast,
                    {.required = false,
                        .numeric = {.minimum = 0.0f, .maximum = 1.0f, .enforce_bounds = true}}),
                make_property_descriptor(
                    "wind", "世界风（米／秒）", &WeatherComponent::wind, {.required = false}),
                make_property_descriptor("fog_density", "雾密度", &WeatherComponent::fog_density,
                    {.required = false,
                        .numeric = {.minimum = 0.0f, .maximum = 2.0f, .enforce_bounds = true}}),
                make_property_descriptor(
                    "fog_color", "雾颜色", &WeatherComponent::fog_color, {.required = false}),
                make_property_descriptor(
                    "sun", "太阳实体", &WeatherComponent::sun, {.required = false}),
                make_property_descriptor("sun_intensity", "日间太阳强度",
                    &WeatherComponent::sun_intensity,
                    {.required = false,
                        .numeric = {.minimum = 0.0f, .maximum = 10000.0f, .enforce_bounds = true}}),
                make_property_descriptor(
                    "sun_yaw", "太阳方位角", &WeatherComponent::sun_yaw, {.required = false}),
                make_property_descriptor("precipitation", "雨／雪发射器",
                    &WeatherComponent::precipitation, {.required = false}),
                make_property_descriptor("precipitation_amount", "降水强度",
                    &WeatherComponent::precipitation_amount,
                    {.required = false,
                        .numeric = {.minimum = 0.0f, .maximum = 1.0f, .enforce_bounds = true}})}));

        register_component(make_component_descriptor<VegetationInteractorComponent>(
            "vegetation_interactor", "植被交互体",
            {make_property_descriptor("radius", "作用半径", &VegetationInteractorComponent::radius,
                 {.required = false,
                     .numeric = {.minimum = .01, .maximum = 20, .enforce_bounds = true}}),
                make_property_descriptor("height", "作用高度",
                    &VegetationInteractorComponent::height,
                    {.required = false,
                        .numeric = {.minimum = .01, .maximum = 100, .enforce_bounds = true}})}));
        register_component(make_component_descriptor<WaterSurfaceComponent>("water_surface",
            "水面波纹",
            {make_property_descriptor(
                 "enabled", "启用", &WaterSurfaceComponent::enabled, {.required = false}),
                make_property_descriptor("amplitude", "法线波纹强度",
                    &WaterSurfaceComponent::amplitude,
                    {.required = false,
                        .numeric = {.minimum = 0.0f, .maximum = 1.0f, .enforce_bounds = true}}),
                make_property_descriptor("wavelength", "波长（米）",
                    &WaterSurfaceComponent::wavelength,
                    {.required = false,
                        .numeric = {.minimum = 0.01f, .maximum = 1000.0f, .enforce_bounds = true}}),
                make_property_descriptor("speed", "相位速度", &WaterSurfaceComponent::speed,
                    {.required = false,
                        .numeric = {
                            .minimum = -10.0f, .maximum = 10.0f, .enforce_bounds = true}})}));

        register_component(make_component_descriptor<ParticleMeshCollisionComponent>(
            "particle_mesh_collision", "粒子网格碰撞",
            {make_property_descriptor(
                 "enabled", "启用", &ParticleMeshCollisionComponent::enabled, {.required = false}),
                make_property_descriptor("mesh", "碰撞网格（可选）",
                    &ParticleMeshCollisionComponent::mesh,
                    {.required = false, .asset_type = AssetType::Mesh, .asset_cpu_only = true}),
                make_property_descriptor("layers", "粒子碰撞层",
                    &ParticleMeshCollisionComponent::layers, {.required = false, .bitmask = true}),
                make_property_descriptor("deformed", "使用蒙皮与柔性姿态",
                    &ParticleMeshCollisionComponent::deformed, {.required = false})}));

        register_component(make_component_descriptor<WindSwayComponent>("wind_sway", "风动摆动",
            {make_property_descriptor("enabled", "启用", &WindSwayComponent::enabled),
                make_property_descriptor(
                    "amplitude", "摆动幅度（度）", &WindSwayComponent::amplitude),
                make_property_descriptor(
                    "flexible", "柔性顶点风动", &WindSwayComponent::flexible, {.required = false}),
                make_property_descriptor("height", "弯曲高度（本地米）", &WindSwayComponent::height,
                    {.required = false,
                        .numeric = {.minimum = .01f, .maximum = 1000, .enforce_bounds = true}}),
                make_property_descriptor("interaction_radius", "角色交互半径（米）",
                    &WindSwayComponent::interaction_radius,
                    {.required = false,
                        .numeric = {.minimum = 0, .maximum = 20, .enforce_bounds = true}}),
                make_property_descriptor("interaction_strength", "交互弯曲（度）",
                    &WindSwayComponent::interaction_strength,
                    {.required = false,
                        .numeric = {.minimum = 0, .maximum = 30, .enforce_bounds = true}}),
                make_property_descriptor("wind_response", "世界风响应",
                    &WindSwayComponent::wind_response,
                    {.required = false,
                        .numeric = {.minimum = 0, .maximum = 10, .enforce_bounds = true}}),
                make_property_descriptor("frequency", "频率（Hz）", &WindSwayComponent::frequency,
                    {.numeric = {.minimum = .01f, .maximum = 10, .enforce_bounds = true}})}));
        register_component(make_component_descriptor<MotionWarpComponent>("motion_warp", "动作对位",
            {make_property_descriptor("enabled", "启用", &MotionWarpComponent::enabled),
                make_property_descriptor("target", "对位目标", &MotionWarpComponent::target),
                make_property_descriptor(
                    "state", "状态／片段（留空匹配全部）", &MotionWarpComponent::state),
                make_property_descriptor("start", "窗口起点（0～1）", &MotionWarpComponent::start,
                    {.numeric =
                            {.speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}}),
                make_property_descriptor("end", "窗口终点（0～1）", &MotionWarpComponent::end,
                    {.numeric =
                            {.speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}}),
                make_property_descriptor("max_distance", "最大轨迹修正 (m)",
                    &MotionWarpComponent::max_distance,
                    {.numeric = {.speed = .1f, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor("max_angle", "最大朝向修正 (°)",
                    &MotionWarpComponent::max_angle,
                    {.numeric =
                            {.speed = 1, .minimum = 0, .maximum = 180, .enforce_bounds = true}}),
                make_property_descriptor("weight", "权重", &MotionWarpComponent::weight,
                    {.numeric = {
                         .speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}})}));
        register_component(make_component_descriptor<FootPlacementComponent>("foot_placement",
            "脚部 IK",
            {make_property_descriptor("enabled", "启用", &FootPlacementComponent::enabled),
                make_property_descriptor(
                    "character", "角色（留空使用父实体）", &FootPlacementComponent::character),
                make_property_descriptor(
                    "left_foot", "左脚骨骼", &FootPlacementComponent::left_foot),
                make_property_descriptor(
                    "right_foot", "右脚骨骼", &FootPlacementComponent::right_foot),
                make_property_descriptor("weight", "权重", &FootPlacementComponent::weight,
                    {.numeric =
                            {.speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}}),
                make_property_descriptor("foot_height", "脚底到踝关节 (m)",
                    &FootPlacementComponent::foot_height,
                    {.numeric = {.speed = .01f, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor("max_offset", "最大高度修正 (m)",
                    &FootPlacementComponent::max_offset,
                    {.numeric = {.speed = .01f, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor("plant_height", "抬脚解除高度 (m)",
                    &FootPlacementComponent::plant_height,
                    {.numeric = {.speed = .01f, .minimum = .001f, .enforce_bounds = true}}),
                make_property_descriptor("probe_up", "向上探测距离 (m)",
                    &FootPlacementComponent::probe_up,
                    {.numeric = {.speed = .01f, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor("probe_down", "向下探测距离 (m)",
                    &FootPlacementComponent::probe_down,
                    {.numeric = {.speed = .01f, .minimum = .001f, .enforce_bounds = true}}),
                make_property_descriptor("max_slope", "最大坡度 (°)",
                    &FootPlacementComponent::max_slope,
                    {.numeric = {.speed = 1, .minimum = 0, .maximum = 89, .enforce_bounds = true}}),
                make_property_descriptor("response", "响应速度", &FootPlacementComponent::response,
                    {.numeric = {.speed = .5f, .minimum = .01f, .enforce_bounds = true}}),
                make_property_descriptor(
                    "knee_forward", "膝盖朝向（模型空间）", &FootPlacementComponent::knee_forward),
                make_property_descriptor(
                    "pelvis", "骨盆骨骼", &FootPlacementComponent::pelvis, {.required = false}),
                make_property_descriptor("pelvis_weight", "骨盆补偿权重",
                    &FootPlacementComponent::pelvis_weight,
                    {.required = false,
                        .numeric =
                            {.speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}}),
                make_property_descriptor("max_pelvis_offset", "骨盆最大下降 (m)",
                    &FootPlacementComponent::max_pelvis_offset,
                    {.required = false,
                        .numeric = {.speed = .01f, .minimum = 0, .enforce_bounds = true}})}));
        register_component(make_component_descriptor<CharacterAnimationComponent>(
            "character_animation", "Character Animation",
            {make_property_descriptor(
                "character", "Character", &CharacterAnimationComponent::character)}));
        register_component(
            make_component_descriptor<BoneAttachmentComponent>("bone_attachment", "骨骼挂点",
                {make_property_descriptor("bone", "骨骼", &BoneAttachmentComponent::bone)}));

        register_component(make_component_descriptor<CameraComponent>("camera", "Camera",
            {make_property_descriptor("primary", "Primary", &CameraComponent::primary),
                make_enum_property_descriptor<CameraComponent, CameraComponent::Projection>(
                    "projection", "Projection", &CameraComponent::projection,
                    {{CameraComponent::Projection::Perspective, {"perspective", "Perspective"}},
                        {CameraComponent::Projection::Orthographic,
                            {"orthographic", "Orthographic"}}},
                    {.required = false}),
                make_property_descriptor("fov", "Perspective FOV", &CameraComponent::fov,
                    {.numeric = {.speed = 1.0f, .minimum = 1.0f, .maximum = 179.0f}}),
                make_property_descriptor("orthographic_height", "Orthographic Height",
                    &CameraComponent::orthographic_height,
                    {.required = false, .numeric = {.speed = 0.1f, .minimum = 0.001f}}),
                make_property_descriptor("near_clip", "Near Clip", &CameraComponent::near_clip,
                    {.numeric = {.speed = 0.01f, .minimum = 0.001f}}),
                make_property_descriptor("far_clip", "Far Clip", &CameraComponent::far_clip,
                    {.numeric = {.speed = 1.0f, .minimum = 0.001f}})}));

        register_component(make_component_descriptor<CameraControllerComponent>("camera_controller",
            "Camera Controller",
            {make_property_descriptor("enabled", "Enabled", &CameraControllerComponent::enabled),
                make_enum_property_descriptor<CameraControllerComponent,
                    CameraControllerComponent::Mode>("mode", "Control Mode",
                    &CameraControllerComponent::mode,
                    {{CameraControllerComponent::Mode::Free, {"free", "Free"}},
                        {CameraControllerComponent::Mode::Orbit, {"orbit", "Orbit"}}},
                    {.required = false}),
                make_property_descriptor("move_speed", "Move Speed",
                    &CameraControllerComponent::move_speed,
                    {.numeric = {.speed = 0.1f, .minimum = 0.0f, .maximum = 1000.0f}}),
                make_property_descriptor("look_sensitivity", "Mouse Look Sensitivity",
                    &CameraControllerComponent::look_sensitivity,
                    {.numeric = {.speed = 0.01f, .minimum = 0.0f, .maximum = 10.0f}}),
                make_property_descriptor("look_speed", "Look Speed (deg/s)",
                    &CameraControllerComponent::look_speed,
                    {.required = false,
                        .numeric = {.speed = 1.0f, .minimum = 0.0f, .maximum = 720.0f}}),
                make_property_descriptor("orbit_distance", "Orbit Distance",
                    &CameraControllerComponent::orbit_distance,
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0.01f, .enforce_bounds = true}}),
                make_property_descriptor("orbit_height", "Orbit Height",
                    &CameraControllerComponent::orbit_height,
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor("orbit_min_distance", "Minimum Orbit Distance",
                    &CameraControllerComponent::orbit_min_distance,
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0.01f, .enforce_bounds = true}}),
                make_property_descriptor("orbit_max_distance", "Maximum Orbit Distance",
                    &CameraControllerComponent::orbit_max_distance,
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0.01f, .enforce_bounds = true}}),
                make_property_descriptor("orbit_zoom_step", "Orbit Zoom Step",
                    &CameraControllerComponent::orbit_zoom_step,
                    {.required = false,
                        .numeric = {.speed = 0.05f, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor("collision_radius", "Camera Collision Radius",
                    &CameraControllerComponent::collision_radius,
                    {.required = false,
                        .numeric = {.speed = 0.01f, .minimum = 0.01f, .enforce_bounds = true}})}));

        register_component(make_component_descriptor<ParticleEmitterComponent>("particle_emitter",
            "Particle Emitter",
            {make_enum_property_descriptor<ParticleEmitterComponent, ParticleSimulation>(
                 "simulation", "模拟方式", &ParticleEmitterComponent::simulation,
                 {{ParticleSimulation::CPU, {"cpu", "CPU（支持物理碰撞）"}},
                     {ParticleSimulation::GPU, {"gpu", "GPU（大规模视觉粒子）"}}},
                 {.required = false}),
                make_property_descriptor("gpu_dynamics", "GPU 有状态模拟／深度碰撞",
                    &ParticleEmitterComponent::gpu_dynamics, {.required = false}),
                make_property_descriptor("gpu_world_collision",
                    "GPU 世界碰撞（基础形状／静态网格）",
                    &ParticleEmitterComponent::gpu_world_collision, {.required = false}),
                make_property_descriptor("mesh_collision_budget", "网格碰撞查询预算",
                    &ParticleEmitterComponent::mesh_collision_budget,
                    {.required = false,
                        .numeric = {.minimum = 16, .maximum = 2048, .enforce_bounds = true}}),
                make_property_descriptor("vector_field", "GPU 三维向量场",
                    &ParticleEmitterComponent::vector_field,
                    {.required = false, .asset_type = AssetType::VectorField}),
                make_property_descriptor("vector_field_strength", "向量场强度",
                    &ParticleEmitterComponent::vector_field_strength,
                    {.required = false,
                        .numeric = {.minimum = -1000, .maximum = 1000, .enforce_bounds = true}}),
                make_property_descriptor("wind", "GPU 风加速度（世界空间）",
                    &ParticleEmitterComponent::wind, {.required = false}),
                make_property_descriptor("vortex_center", "GPU 涡旋中心（世界空间）",
                    &ParticleEmitterComponent::vortex_center, {.required = false}),
                make_property_descriptor("vortex_axis", "GPU 涡旋轴",
                    &ParticleEmitterComponent::vortex_axis, {.required = false}),
                make_property_descriptor("vortex_strength", "GPU 涡旋加速度",
                    &ParticleEmitterComponent::vortex_strength,
                    {.required = false,
                        .numeric = {.minimum = -1000, .maximum = 1000, .enforce_bounds = true}}),
                make_property_descriptor("vortex_radius", "GPU 涡旋半径（米）",
                    &ParticleEmitterComponent::vortex_radius,
                    {.required = false,
                        .numeric = {.minimum = .01f, .maximum = 100000, .enforce_bounds = true}}),
                make_property_descriptor("turbulence", "GPU 扰动加速度",
                    &ParticleEmitterComponent::turbulence,
                    {.required = false,
                        .numeric =
                            {.speed = .1f, .minimum = 0, .maximum = 1000, .enforce_bounds = true}}),
                make_property_descriptor("turbulence_frequency", "GPU 扰动频率",
                    &ParticleEmitterComponent::turbulence_frequency,
                    {.required = false,
                        .numeric = {.speed = .1f,
                            .minimum = .01f,
                            .maximum = 100,
                            .enforce_bounds = true}}),
                make_property_descriptor("texture", "Particle Texture",
                    &ParticleEmitterComponent::texture,
                    {.required = false, .asset_type = AssetType::Texture}),
                make_enum_property_descriptor<ParticleEmitterComponent, ParticleBlend>("blend",
                    "Blend", &ParticleEmitterComponent::blend,
                    {{ParticleBlend::Additive, {"additive", "加法（火花／发光）"}},
                        {ParticleBlend::Alpha, {"alpha", "Alpha（烟雾／半透明）"}}},
                    {.required = false}),
                make_property_descriptor("soft_distance", "交界淡出距离（米，0 关闭）",
                    &ParticleEmitterComponent::soft_distance,
                    {.required = false,
                        .numeric = {.speed = 0.01f,
                            .minimum = 0,
                            .maximum = 1000,
                            .enforce_bounds = true}}),
                make_property_descriptor("atlas_columns", "图集列数",
                    &ParticleEmitterComponent::atlas_columns,
                    {.required = false,
                        .numeric =
                            {.speed = 0.1f, .minimum = 1, .maximum = 64, .enforce_bounds = true}}),
                make_property_descriptor("atlas_rows", "图集行数",
                    &ParticleEmitterComponent::atlas_rows,
                    {.required = false,
                        .numeric =
                            {.speed = 0.1f, .minimum = 1, .maximum = 64, .enforce_bounds = true}}),
                make_property_descriptor("frame_count", "有效帧数",
                    &ParticleEmitterComponent::frame_count,
                    {.required = false,
                        .numeric = {.speed = 0.1f,
                            .minimum = 1,
                            .maximum = 4096,
                            .enforce_bounds = true}}),
                make_property_descriptor("frame_rate", "帧率（0 按寿命播放）",
                    &ParticleEmitterComponent::frame_rate,
                    {.required = false,
                        .numeric =
                            {.speed = 0.1f, .minimum = 0, .maximum = 240, .enforce_bounds = true}}),
                make_property_descriptor("loop_animation", "循环序列帧",
                    &ParticleEmitterComponent::loop_animation, {.required = false}),
                make_enum_property_descriptor<ParticleEmitterComponent, ParticleCollision>(
                    "collision", "粒子碰撞", &ParticleEmitterComponent::collision,
                    {{ParticleCollision::None, {"none", "关闭"}},
                        {ParticleCollision::Bounce, {"bounce", "反弹"}},
                        {ParticleCollision::Kill, {"kill", "碰撞后消失"}}},
                    {.required = false}),
                make_property_descriptor("collision_radius", "碰撞半径（米，0 使用射线）",
                    &ParticleEmitterComponent::collision_radius,
                    {.required = false,
                        .numeric =
                            {.speed = 0.01f, .minimum = 0, .maximum = 10, .enforce_bounds = true}}),
                make_property_descriptor("restitution", "碰撞弹性",
                    &ParticleEmitterComponent::restitution,
                    {.required = false,
                        .numeric =
                            {.speed = 0.01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}}),
                make_property_descriptor("friction", "碰撞摩擦",
                    &ParticleEmitterComponent::friction,
                    {.required = false,
                        .numeric =
                            {.speed = 0.01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}}),
                make_property_descriptor("collision_layers", "碰撞层掩码",
                    &ParticleEmitterComponent::collision_layers,
                    {.required = false, .bitmask = true}),
                make_property_descriptor(
                    "enabled", "Enabled", &ParticleEmitterComponent::enabled, {.required = false}),
                make_property_descriptor("capacity", "Capacity",
                    &ParticleEmitterComponent::capacity,
                    {.required = false,
                        .numeric = {.speed = 0.1f,
                            .minimum = 1,
                            .maximum = 65536,
                            .enforce_bounds = true}}),
                make_property_descriptor(
                    "seed", "Random Seed", &ParticleEmitterComponent::seed, {.required = false}),
                make_property_descriptor("burst", "Startup Burst", &ParticleEmitterComponent::burst,
                    {.required = false,
                        .numeric = {.speed = 0.1f,
                            .minimum = 0,
                            .maximum = 65536,
                            .enforce_bounds = true}}),
                make_property_descriptor("rate", "Emission Rate", &ParticleEmitterComponent::rate,
                    {.required = false,
                        .numeric = {.speed = 0.1f,
                            .minimum = 0,
                            .maximum = 1000000,
                            .enforce_bounds = true}}),
                make_property_descriptor("lifetime", "Lifetime",
                    &ParticleEmitterComponent::lifetime,
                    {.required = false,
                        .numeric = {.speed = 0.1f,
                            .minimum = 0.01,
                            .maximum = 60,
                            .enforce_bounds = true}}),
                make_property_descriptor("speed", "Speed", &ParticleEmitterComponent::speed,
                    {.required = false,
                        .numeric = {.speed = 0.1f,
                            .minimum = 0,
                            .maximum = 1000,
                            .enforce_bounds = true}}),
                make_property_descriptor("spread", "Cone Angle", &ParticleEmitterComponent::spread,
                    {.required = false,
                        .numeric =
                            {.speed = 0.1f, .minimum = 0, .maximum = 180, .enforce_bounds = true}}),
                make_property_descriptor("acceleration", "Acceleration",
                    &ParticleEmitterComponent::acceleration, {.required = false}),
                make_property_descriptor("start_color", "Start Color",
                    &ParticleEmitterComponent::start_color, {.required = false}),
                make_property_descriptor("end_color", "End Color",
                    &ParticleEmitterComponent::end_color, {.required = false}),
                make_property_descriptor("spawn_extents", "发射盒半尺寸",
                    &ParticleEmitterComponent::spawn_extents,
                    {.required = false,
                        .numeric = {.speed = .1f,
                            .minimum = 0,
                            .maximum = 10000,
                            .enforce_bounds = true}}),
                make_property_descriptor("start_size", "Start Size",
                    &ParticleEmitterComponent::start_size,
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor("end_size", "End Size",
                    &ParticleEmitterComponent::end_size,
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}})}));

        register_component(make_component_descriptor<TrailComponent>("trail", "拖尾",
            {make_property_descriptor(
                 "enabled", "启用", &TrailComponent::enabled, {.required = false}),
                make_property_descriptor("texture", "拖尾纹理", &TrailComponent::texture,
                    {.required = false, .asset_type = AssetType::Texture}),
                make_enum_property_descriptor<TrailComponent, ParticleBlend>("blend", "混合方式",
                    &TrailComponent::blend,
                    {{ParticleBlend::Additive, {"additive", "加法"}},
                        {ParticleBlend::Alpha, {"alpha", "Alpha"}}},
                    {.required = false}),
                make_property_descriptor("capacity", "最多采样点", &TrailComponent::capacity,
                    {.required = false,
                        .numeric =
                            {.speed = 1, .minimum = 2, .maximum = 512, .enforce_bounds = true}}),
                make_property_descriptor("lifetime", "保留时间（秒）", &TrailComponent::lifetime,
                    {.required = false,
                        .numeric = {.speed = 0.01f,
                            .minimum = 0.01f,
                            .maximum = 60,
                            .enforce_bounds = true}}),
                make_property_descriptor("minimum_distance", "采样间距（米）",
                    &TrailComponent::minimum_distance,
                    {.required = false,
                        .numeric = {.speed = 0.01f, .minimum = 0.001f, .enforce_bounds = true}}),
                make_property_descriptor("break_distance", "瞬移断开距离（米）",
                    &TrailComponent::break_distance,
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0.01f, .enforce_bounds = true}}),
                make_property_descriptor("width", "宽度（米）", &TrailComponent::width,
                    {.required = false,
                        .numeric = {.speed = 0.01f, .minimum = 0.001f, .enforce_bounds = true}}),
                make_property_descriptor(
                    "color", "颜色", &TrailComponent::color, {.required = false}),
                make_property_descriptor("soft_distance", "交界淡出距离（米）",
                    &TrailComponent::soft_distance,
                    {.required = false,
                        .numeric = {.speed = 0.01f,
                            .minimum = 0,
                            .maximum = 1000,
                            .enforce_bounds = true}})}));

        register_component(
            make_component_descriptor<AudioListenerComponent>("audio_listener", "Audio Listener",
                {make_property_descriptor(
                    "enabled", "Enabled", &AudioListenerComponent::enabled, {.required = false})}));

        register_component(
            make_component_descriptor<AudioSourceComponent>("audio_source", "Audio Source",
                {make_property_descriptor("clip", "Audio Clip", &AudioSourceComponent::clip,
                     {.asset_type = AssetType::Audio}),
                    make_property_descriptor(
                        "play_on_start", "Play On Start", &AudioSourceComponent::play_on_start),
                    make_property_descriptor("loop", "Loop", &AudioSourceComponent::loop),
                    make_enum_property_descriptor<AudioSourceComponent, AudioCategory>("category",
                        "Audio Category", &AudioSourceComponent::category,
                        {{AudioCategory::Effects, {"effects", "Sound Effects"}},
                            {AudioCategory::Music, {"music", "Music"}}},
                        {.required = false}),
                    make_property_descriptor("volume", "Volume", &AudioSourceComponent::volume,
                        {.numeric = {.speed = 0.01f,
                             .minimum = 0.0f,
                             .maximum = 1.0f,
                             .enforce_bounds = true}}),
                    make_property_descriptor("spatial", "Spatial Audio",
                        &AudioSourceComponent::spatial, {.required = false}),
                    make_property_descriptor("min_distance", "Min Distance",
                        &AudioSourceComponent::min_distance,
                        {.required = false,
                            .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}}),
                    make_property_descriptor("max_distance", "Max Distance",
                        &AudioSourceComponent::max_distance,
                        {.required = false,
                            .numeric = {.speed = 0.1f, .minimum = 0.01f, .enforce_bounds = true}}),
                    make_property_descriptor("rolloff", "Rolloff", &AudioSourceComponent::rolloff,
                        {.required = false,
                            .numeric = {.speed = 0.05f, .minimum = 0, .enforce_bounds = true}}),
                    make_property_descriptor("fade_in", "淡入时间（秒）",
                        &AudioSourceComponent::fade_in,
                        {.required = false,
                            .numeric = {.speed = .01f,
                                .minimum = 0.0f,
                                .maximum = 60.0f,
                                .enforce_bounds = true}}),
                    make_property_descriptor("fade_out", "淡出时间（秒）",
                        &AudioSourceComponent::fade_out,
                        {.required = false,
                            .numeric = {.speed = .01f,
                                .minimum = 0.0f,
                                .maximum = 60.0f,
                                .enforce_bounds = true}}),
                    make_property_descriptor("reverb_mix", "混响比例",
                        &AudioSourceComponent::reverb_mix,
                        {.required = false,
                            .numeric = {.speed = .01f,
                                .minimum = 0.0f,
                                .maximum = 1.0f,
                                .enforce_bounds = true}}),
                    make_property_descriptor("reverb_decay", "混响衰减（秒）",
                        &AudioSourceComponent::reverb_decay,
                        {.required = false,
                            .numeric = {.speed = .01f,
                                .minimum = 0.1f,
                                .maximum = 10.0f,
                                .enforce_bounds = true}}),
                    make_property_descriptor("occlusion_strength", "遮挡强度",
                        &AudioSourceComponent::occlusion_strength,
                        {.required = false,
                            .numeric = {.speed = .01f,
                                .minimum = 0.0f,
                                .maximum = 1.0f,
                                .enforce_bounds = true}}),
                    make_property_descriptor("occlusion", "几何遮挡",
                        &AudioSourceComponent::occlusion, {.required = false}),
                    make_property_descriptor("occlusion_layers", "遮挡碰撞层",
                        &AudioSourceComponent::occlusion_layers,
                        {.required = false, .bitmask = true})}));

        register_component(make_component_descriptor<RigidBodyComponent>("rigid_body", "Rigid Body",
            {make_enum_property_descriptor<RigidBodyComponent, BodyMotion>("motion", "Motion",
                 &RigidBodyComponent::motion,
                 {{BodyMotion::Static, {"static", "Static"}},
                     {BodyMotion::Dynamic, {"dynamic", "Dynamic"}},
                     {BodyMotion::Kinematic, {"kinematic", "Kinematic"}}}),
                make_enum_property_descriptor<RigidBodyComponent, BodyMassMode>("mass_mode",
                    "Mass Mode", &RigidBodyComponent::mass_mode,
                    {{BodyMassMode::Mass, {"mass", "Mass"}},
                        {BodyMassMode::Density, {"density", "Density"}}},
                    {.required = false}),
                make_property_descriptor("mass", "Mass (kg)", &RigidBodyComponent::mass,
                    {.required = false,
                        .numeric = {.speed = 0.1f,
                            .minimum = RigidBodyComponent::MIN_MASS,
                            .enforce_bounds = true}}),
                make_property_descriptor("density", "Density (kg/m³)", &RigidBodyComponent::density,
                    {.required = false,
                        .numeric = {.speed = 1.0f,
                            .minimum = RigidBodyComponent::MIN_DENSITY,
                            .enforce_bounds = true}}),
                make_property_descriptor("continuous_collision", "Continuous Collision (CCD)",
                    &RigidBodyComponent::continuous_collision, {.required = false})}));

        register_component(make_component_descriptor<CharacterControllerComponent>(
            "character_controller", "Character Controller",
            {make_property_descriptor("enabled", "Enabled", &CharacterControllerComponent::enabled),
                make_property_descriptor("radius", "Capsule Radius",
                    &CharacterControllerComponent::radius,
                    {.numeric = {.speed = 0.01f, .minimum = 0.01f, .enforce_bounds = true}}),
                make_property_descriptor("height", "Capsule Height",
                    &CharacterControllerComponent::height,
                    {.numeric = {.speed = 0.05f, .minimum = 0.03f, .enforce_bounds = true}}),
                make_property_descriptor("move_speed", "Move Speed",
                    &CharacterControllerComponent::move_speed,
                    {.numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor("run_speed", "Run Speed",
                    &CharacterControllerComponent::run_speed,
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor("crouch_speed", "Crouch Speed",
                    &CharacterControllerComponent::crouch_speed,
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor("crouch_height_ratio", "Crouch Height Ratio",
                    &CharacterControllerComponent::crouch_height_ratio,
                    {.required = false,
                        .numeric = {.speed = 0.01f,
                            .minimum = 0.1f,
                            .maximum = 1,
                            .enforce_bounds = true}}),
                make_property_descriptor("air_control", "Air Control",
                    &CharacterControllerComponent::air_control,
                    {.required = false,
                        .numeric =
                            {.speed = 0.01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}}),
                make_property_descriptor("push_force", "Push Force (N)",
                    &CharacterControllerComponent::push_force,
                    {.required = false,
                        .numeric = {.speed = 1, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor("jump_speed", "Jump Speed",
                    &CharacterControllerComponent::jump_speed,
                    {.numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor("max_slope_angle", "Maximum Slope (degrees)",
                    &CharacterControllerComponent::max_slope_angle,
                    {.numeric = {.speed = 1, .minimum = 1, .maximum = 89, .enforce_bounds = true}}),
                make_property_descriptor("step_height", "Step Height",
                    &CharacterControllerComponent::step_height,
                    {.numeric = {.speed = 0.01f, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor("ground_snap_distance", "Ground Snap Distance",
                    &CharacterControllerComponent::ground_snap_distance,
                    {.numeric = {.speed = 0.01f, .minimum = 0, .enforce_bounds = true}}),
                make_property_descriptor("collision_layer", "Collision Layer",
                    &CharacterControllerComponent::collision_layer,
                    {.required = false, .bitmask = true}),
                make_property_descriptor("collision_mask", "Collision Mask",
                    &CharacterControllerComponent::collision_mask,
                    {.required = false, .bitmask = true}),
                make_property_descriptor("gravity_scale", "Gravity Scale",
                    &CharacterControllerComponent::gravity_scale,
                    {.numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}})}));

        register_component(make_component_descriptor<ColliderComponent>("collider", "Collider",
            {make_enum_property_descriptor<ColliderComponent, ColliderShape>("shape", "Shape",
                 &ColliderComponent::shape,
                 {{ColliderShape::Box, {"box", "Box"}},
                     {ColliderShape::Sphere, {"sphere", "Sphere"}},
                     {ColliderShape::Capsule, {"capsule", "Capsule"}},
                     {ColliderShape::ConvexHull, {"convex_hull", "Convex Hull"}},
                     {ColliderShape::TriangleMesh, {"triangle_mesh", "Static Triangle Mesh"}}}),
                make_property_descriptor("mesh", "Collision Mesh", &ColliderComponent::mesh,
                    {.required = false, .asset_type = AssetType::Mesh, .asset_cpu_only = true}),
                make_property_descriptor("collision_layer", "Collision Layer",
                    &ColliderComponent::collision_layer, {.required = false, .bitmask = true}),
                make_property_descriptor("collision_mask", "Collision Mask",
                    &ColliderComponent::collision_mask, {.required = false, .bitmask = true}),
                make_property_descriptor("half_extents", "Box Half Extents",
                    &ColliderComponent::half_extents,
                    {.numeric = {.speed = 0.05f, .minimum = 0.001f}}),
                make_property_descriptor("radius", "Radius", &ColliderComponent::radius,
                    {.numeric = {.speed = 0.05f, .minimum = 0.001f}}),
                make_property_descriptor("height", "Capsule Height", &ColliderComponent::height,
                    {.required = false, .numeric = {.speed = 0.05f, .minimum = 0.001f}}),
                make_property_descriptor(
                    "is_trigger", "Trigger", &ColliderComponent::is_trigger, {.required = false}),
                make_property_descriptor("friction", "Friction", &ColliderComponent::friction,
                    {.required = false,
                        .numeric =
                            {.speed = 0.01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}}),
                make_property_descriptor("restitution", "Restitution",
                    &ColliderComponent::restitution,
                    {.required = false,
                        .numeric = {.speed = 0.01f,
                            .minimum = 0,
                            .maximum = 1,
                            .enforce_bounds = true}})}));

        register_component(make_component_descriptor<PointJointComponent>(
            "point_joint", "Point Joint", joint_properties<PointJointComponent>()));
        auto hinge_properties = axis_joint_properties<HingeJointComponent>();
        hinge_properties.insert(hinge_properties.end(),
            {make_property_descriptor("limits_enabled", "Angle Limits",
                 &HingeJointComponent::limits_enabled, {.required = false}),
                make_property_descriptor("reference_axis", "Local Reference",
                    &HingeJointComponent::reference_axis, {.required = false}),
                make_property_descriptor("connected_reference_axis", "Connected Reference",
                    &HingeJointComponent::connected_reference_axis, {.required = false}),
                make_property_descriptor("min_angle", "Minimum Angle",
                    &HingeJointComponent::min_angle,
                    {.required = false,
                        .numeric =
                            {.speed = 1, .minimum = -180, .maximum = 0, .enforce_bounds = true}}),
                make_property_descriptor("max_angle", "Maximum Angle",
                    &HingeJointComponent::max_angle,
                    {.required = false,
                        .numeric = {
                            .speed = 1, .minimum = 0, .maximum = 180, .enforce_bounds = true}})});
        append_joint_controls<HingeJointComponent>(hinge_properties,
            make_property_descriptor("motor_target", "Target Angle",
                &HingeJointComponent::motor_target,
                {.required = false,
                    .numeric =
                        {.speed = 1, .minimum = -180, .maximum = 180, .enforce_bounds = true}}),
            make_property_descriptor("motor_speed", "Target Speed",
                &HingeJointComponent::motor_speed, {.required = false, .numeric = {.speed = 1}}),
            make_property_descriptor("motor_max_torque", "Maximum Torque",
                &HingeJointComponent::motor_max_torque,
                {.required = false,
                    .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}}));
        register_component(make_component_descriptor<HingeJointComponent>(
            "hinge_joint", "Hinge Joint", std::move(hinge_properties)));

        auto slider_properties = axis_joint_properties<SliderJointComponent>();
        slider_properties.insert(slider_properties.end(),
            {make_property_descriptor("reference_axis", "Local Orientation",
                 &SliderJointComponent::reference_axis, {.required = false}),
                make_property_descriptor("connected_reference_axis", "Connected Orientation",
                    &SliderJointComponent::connected_reference_axis, {.required = false}),
                make_property_descriptor("limits_enabled", "Travel Limits",
                    &SliderJointComponent::limits_enabled, {.required = false}),
                make_property_descriptor("min_distance", "Minimum Distance",
                    &SliderJointComponent::min_distance,
                    {.required = false,
                        .numeric = {.speed = 0.01f, .maximum = 0, .enforce_bounds = true}}),
                make_property_descriptor("max_distance", "Maximum Distance",
                    &SliderJointComponent::max_distance,
                    {.required = false,
                        .numeric = {.speed = 0.01f, .minimum = 0, .enforce_bounds = true}})});
        append_joint_controls<SliderJointComponent>(slider_properties,
            make_property_descriptor("motor_target", "Target Distance",
                &SliderJointComponent::motor_target,
                {.required = false, .numeric = {.speed = 0.01f}}),
            make_property_descriptor("motor_speed", "Target Speed",
                &SliderJointComponent::motor_speed,
                {.required = false, .numeric = {.speed = 0.01f}}),
            make_property_descriptor("motor_max_force", "Maximum Force",
                &SliderJointComponent::motor_max_force,
                {.required = false,
                    .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}}));
        register_component(make_component_descriptor<SliderJointComponent>(
            "slider_joint", "Slider Joint", std::move(slider_properties)));

        register_component(make_component_descriptor<LightComponent>("light", "Light",
            {make_enum_property_descriptor<LightComponent, LightType>("type", "Type",
                 &LightComponent::type,
                 {{LightType::Directional, {"directional", "Directional"}},
                     {LightType::Point, {"point", "Point"}}, {LightType::Spot, {"spot", "Spot"}}}),
                make_property_descriptor("enabled", "Enabled", &LightComponent::enabled),
                make_property_descriptor("casts_shadow", "Cast shadow",
                    &LightComponent::casts_shadow, {.required = false}),
                make_property_descriptor("shadow_distance", "方向光阴影距离（米）",
                    &LightComponent::shadow_distance,
                    {.required = false,
                        .numeric =
                            {.speed = 1, .minimum = 1, .maximum = 10000, .enforce_bounds = true}}),
                make_property_descriptor("color", "Color (linear)", &LightComponent::color,
                    {.numeric = {.speed = 0.01f, .minimum = 0.0f, .maximum = 1.0f}}),
                make_property_descriptor("intensity", "Intensity", &LightComponent::intensity,
                    {.numeric = {.speed = 0.1f, .minimum = 0.0f, .maximum = 10000.0f}}),
                make_property_descriptor("range", "Range", &LightComponent::range,
                    {.numeric = {.speed = 0.1f, .minimum = 0.001f, .maximum = 1000000.0f}}),
                make_property_descriptor("inner_angle", "Inner angle", &LightComponent::inner_angle,
                    {.numeric = {.speed = 0.5f, .minimum = 0.0f, .maximum = 89.0f}}),
                make_property_descriptor("outer_angle", "Outer angle", &LightComponent::outer_angle,
                    {.numeric = {.speed = 0.5f, .minimum = 0.01f, .maximum = 89.0f}})}));

        register_component(make_component_descriptor<DecalComponent>("decal", "贴花",
            {make_property_descriptor(
                 "enabled", "启用", &DecalComponent::enabled, {.required = false}),
                make_property_descriptor("texture", "颜色贴图", &DecalComponent::texture,
                    {.required = false, .asset_type = AssetType::Texture}),
                make_property_descriptor("half_extents", "半尺寸（本地米）",
                    &DecalComponent::half_extents,
                    {.required = false,
                        .numeric = {.speed = .05f,
                            .minimum = .01f,
                            .maximum = 1000,
                            .enforce_bounds = true}}),
                make_property_descriptor("color", "颜色／不透明度", &DecalComponent::color,
                    {.required = false,
                        .numeric = {.minimum = 0, .maximum = 1, .enforce_bounds = true}}),
                make_property_descriptor("roughness", "粗糙度", &DecalComponent::roughness,
                    {.required = false,
                        .numeric = {.speed = .01f,
                            .minimum = .045f,
                            .maximum = 1,
                            .enforce_bounds = true}}),
                make_property_descriptor("normal_threshold", "表面朝向阈值",
                    &DecalComponent::normal_threshold,
                    {.required = false,
                        .numeric =
                            {.speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}}),
                make_property_descriptor("edge_fade", "边缘淡出", &DecalComponent::edge_fade,
                    {.required = false,
                        .numeric =
                            {.speed = .01f, .minimum = 0, .maximum = .5f, .enforce_bounds = true}}),
                make_property_descriptor("draw_order", "覆盖顺序", &DecalComponent::draw_order,
                    {.required = false,
                        .numeric = {
                            .speed = 1, .minimum = 0, .maximum = 1000, .enforce_bounds = true}})}));

        register_component(make_component_descriptor<ReflectionProbeComponent>("reflection_probe",
            "局部反射探针",
            {make_property_descriptor("enabled", "启用", &ReflectionProbeComponent::enabled),
                make_property_descriptor("environment", "环境贴图",
                    &ReflectionProbeComponent::environment, {.asset_type = AssetType::Environment}),
                make_property_descriptor("half_extents", "影响范围半尺寸（米）",
                    &ReflectionProbeComponent::half_extents,
                    {.numeric = {.speed = .1f,
                         .minimum = .01f,
                         .maximum = 100000,
                         .enforce_bounds = true}}),
                make_property_descriptor("blend_distance", "边缘过渡（米）",
                    &ReflectionProbeComponent::blend_distance,
                    {.numeric = {.speed = .1f,
                         .minimum = 0,
                         .maximum = 100000,
                         .enforce_bounds = true}}),
                make_property_descriptor("intensity", "反射强度",
                    &ReflectionProbeComponent::intensity,
                    {.numeric =
                            {.speed = .05f, .minimum = 0, .maximum = 64, .enforce_bounds = true}}),
                make_property_descriptor(
                    "rotation", "贴图旋转（度）", &ReflectionProbeComponent::rotation),
                make_property_descriptor(
                    "box_projection", "盒投影校正", &ReflectionProbeComponent::box_projection),
                make_property_descriptor("capture_resolution", "烘焙面尺寸",
                    &ReflectionProbeComponent::capture_resolution,
                    {.required = false,
                        .numeric =
                            {.speed = 1, .minimum = 16, .maximum = 512, .enforce_bounds = true}}),
                make_property_descriptor("capture_distance", "烘焙距离（米）",
                    &ReflectionProbeComponent::capture_distance,
                    {.required = false,
                        .numeric = {.speed = 1,
                            .minimum = 0.1f,
                            .maximum = 100000,
                            .enforce_bounds = true}}),
                make_property_descriptor("bake_fingerprint", "烘焙输入",
                    &ReflectionProbeComponent::bake_fingerprint,
                    {.editable = false, .required = false})}));
        register_component(make_component_descriptor<IrradianceVolumeComponent>("irradiance_volume",
            "探针 GI 区域",
            {make_property_descriptor("enabled", "启用", &IrradianceVolumeComponent::enabled),
                make_property_descriptor("realtime", "实时更新",
                    &IrradianceVolumeComponent::realtime, {.required = false}),
                make_property_descriptor("update_budget", "区域每帧上限",
                    &IrradianceVolumeComponent::update_budget,
                    {.required = false,
                        .numeric =
                            {.speed = 1, .minimum = 1, .maximum = 4096, .enforce_bounds = true}}),
                make_property_descriptor("update_rays", "每次更新光线数",
                    &IrradianceVolumeComponent::update_rays,
                    {.required = false,
                        .numeric =
                            {.speed = 16, .minimum = 64, .maximum = 256, .enforce_bounds = true}}),
                make_property_descriptor("bounce_strength", "实时多跳反馈（0 关闭）",
                    &IrradianceVolumeComponent::bounce_strength,
                    {.required = false,
                        .numeric = {.minimum = 0, .maximum = 1, .enforce_bounds = true}}),
                make_property_descriptor("history_weight", "历史权重",
                    &IrradianceVolumeComponent::history_weight,
                    {.required = false,
                        .numeric = {.speed = .01f,
                            .minimum = 0,
                            .maximum = .99f,
                            .enforce_bounds = true}}),
                make_property_descriptor("irradiance", "烘焙光照",
                    &IrradianceVolumeComponent::irradiance, {.asset_type = AssetType::Irradiance}),
                make_property_descriptor("half_extents", "区域半尺寸（米）",
                    &IrradianceVolumeComponent::half_extents,
                    {.numeric = {.speed = .1f,
                         .minimum = .01f,
                         .maximum = 100000,
                         .enforce_bounds = true}}),
                make_property_descriptor("spacing", "探针间距（米）",
                    &IrradianceVolumeComponent::spacing,
                    {.numeric = {.speed = .1f,
                         .minimum = .01f,
                         .maximum = 10000,
                         .enforce_bounds = true}}),
                make_property_descriptor("max_distance", "烘焙距离（米）",
                    &IrradianceVolumeComponent::max_distance,
                    {.numeric = {.speed = 1,
                         .minimum = .1f,
                         .maximum = 100000,
                         .enforce_bounds = true}}),
                make_property_descriptor("ray_samples", "每探针光线数",
                    &IrradianceVolumeComponent::ray_samples,
                    {.numeric =
                            {.speed = 32, .minimum = 32, .maximum = 8192, .enforce_bounds = true}}),
                make_property_descriptor("bounces", "漫反射反弹数",
                    &IrradianceVolumeComponent::bounces,
                    {.numeric = {.speed = 1, .minimum = 0, .maximum = 4, .enforce_bounds = true}}),
                make_property_descriptor("intensity", "间接光强度",
                    &IrradianceVolumeComponent::intensity,
                    {.numeric =
                            {.speed = .05f, .minimum = 0, .maximum = 64, .enforce_bounds = true}}),
                make_property_descriptor("normal_bias", "采样偏移（米）",
                    &IrradianceVolumeComponent::normal_bias,
                    {.numeric =
                            {.speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}}),
                make_property_descriptor("blend_distance", "混合距离（米）",
                    &IrradianceVolumeComponent::blend_distance,
                    {.required = false,
                        .numeric = {.speed = .1f,
                            .minimum = 0,
                            .maximum = 100000,
                            .enforce_bounds = true}}),
                make_property_descriptor("bake_fingerprint", "烘焙输入",
                    &IrradianceVolumeComponent::bake_fingerprint,
                    {.editable = false, .required = false})}));
        return registry;
    }
}

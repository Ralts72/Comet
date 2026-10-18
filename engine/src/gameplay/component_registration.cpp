#include "scene/component_registration.h"
#include "scene/component_changes.h"

namespace Comet {
    void ComponentRegistration::gameplay(ComponentRegistry& registry) {
        add(registry,
            describe_component<NavigationRegionComponent>("navigation_region", "导航区域")
                .field(COMET_FIELD(NavigationRegionComponent, data), "导航产品",
                    {.required = false, .asset_type = AssetType::Navigation})
                .field(COMET_FIELD(NavigationRegionComponent, half_extents), "半尺寸（米）",
                    {.required = false,
                        .numeric = {.speed = 0.05f,
                            .minimum = 0.1,
                            .maximum = 1000,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(NavigationRegionComponent, cell_size), "网格精度（米）",
                    {.required = false,
                        .numeric = {.speed = 0.05f,
                            .minimum = 0.05,
                            .maximum = 2,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(NavigationRegionComponent, agent_radius), "角色半径（米）",
                    {.required = false,
                        .numeric = {.speed = 0.05f,
                            .minimum = 0.05,
                            .maximum = 10,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(NavigationRegionComponent, agent_height), "角色高度（米）",
                    {.required = false,
                        .numeric = {.speed = 0.05f,
                            .minimum = 0.1,
                            .maximum = 20,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(NavigationRegionComponent, max_climb), "台阶高度（米）",
                    {.required = false,
                        .numeric =
                            {.speed = 0.05f, .minimum = 0, .maximum = 2, .enforce_bounds = true}})
                .field(COMET_FIELD(NavigationRegionComponent, max_slope), "最大坡度（度）",
                    {.required = false,
                        .numeric =
                            {.speed = 0.05f, .minimum = 0, .maximum = 89, .enforce_bounds = true}})
                .build());

        add(registry,
            describe_component<NavigationObstacleComponent>("navigation_obstacle", "动态导航障碍")
                .field(COMET_FIELD(NavigationObstacleComponent, enabled), "启用阻塞",
                    {.required = false})
                .field(COMET_FIELD(NavigationObstacleComponent, half_extents), "半尺寸（本地米）",
                    {.required = false,
                        .numeric = {.speed = .05f,
                            .minimum = .01f,
                            .maximum = 1000,
                            .enforce_bounds = true}})
                .build());

        add(registry,
            describe_component<NavigationLinkComponent>("navigation_link", "导航连接（跳跃）")
                .field(COMET_FIELD(NavigationLinkComponent, end), "终点（本地坐标）",
                    {.required = false})
                .field(COMET_FIELD(NavigationLinkComponent, radius), "起降半径（米）",
                    {.required = false,
                        .numeric = {.speed = .05f,
                            .minimum = .05f,
                            .maximum = 10,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(NavigationLinkComponent, bidirectional), "允许往返",
                    {.required = false})
                .build());

        add(registry,
            describe_component<NavigationAgentComponent>("navigation_agent", "导航角色")
                .field(COMET_FIELD(NavigationAgentComponent, enabled), "启用", {.required = false})
                .field(
                    COMET_FIELD(NavigationAgentComponent, facing), "朝向实体", {.required = false})
                .field(COMET_FIELD(NavigationAgentComponent, arrival_distance), "到达距离（米）",
                    {.required = false,
                        .numeric = {.speed = 0.05f,
                            .minimum = 0.05,
                            .maximum = 5,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(NavigationAgentComponent, repath_interval), "重寻路间隔（秒）",
                    {.required = false,
                        .numeric = {.speed = 0.05f,
                            .minimum = 0.05,
                            .maximum = 10,
                            .enforce_bounds = true}})
                .build());

        add(registry,
            describe_component<StreamCellComponent>("stream_cell", "流式世界单元")
                .field(COMET_FIELD(StreamCellComponent, enabled), "启用", {.required = false})
                .field(COMET_FIELD(StreamCellComponent, prefab), "单元 Prefab",
                    {.asset_type = AssetType::Prefab})
                .field(COMET_FIELD(StreamCellComponent, load_distance), "加载距离",
                    {.required = false,
                        .numeric = {.minimum = 1, .maximum = 100000, .enforce_bounds = true}})
                .field(COMET_FIELD(StreamCellComponent, unload_distance), "卸载距离",
                    {.required = false,
                        .numeric = {.minimum = 1, .maximum = 100000, .enforce_bounds = true}})
                .build());

        add(registry,
            describe_component<SplineComponent>("spline", "三次样条")
                .field(COMET_FIELD(SplineComponent, start), "起点", {.required = false})
                .field(COMET_FIELD(SplineComponent, control_a), "控制点 A", {.required = false})
                .field(COMET_FIELD(SplineComponent, control_b), "控制点 B", {.required = false})
                .field(COMET_FIELD(SplineComponent, end), "终点", {.required = false})
                .build());

        add(registry,
            describe_component<SplineFollowerComponent>("spline_follower", "样条跟随／过场")
                .field(COMET_FIELD(SplineFollowerComponent, path), "路径实体", {.required = false})
                .field(COMET_FIELD(SplineFollowerComponent, playing), "播放", {.required = false})
                .field(COMET_FIELD(SplineFollowerComponent, duration), "时长（秒）",
                    {.required = false,
                        .numeric = {.minimum = 0.01f, .maximum = 86400.0f, .enforce_bounds = true}})
                .field(COMET_FIELD(SplineFollowerComponent, loop), "循环", {.required = false})
                .field(
                    COMET_FIELD(SplineFollowerComponent, orient), "沿路径转向", {.required = false})
                .field(COMET_FIELD(SplineFollowerComponent, ease), "缓入缓出", {.required = false})
                .field(COMET_FIELD(SplineFollowerComponent, skip_action), "跳过输入动作",
                    {.required = false})
                .field(COMET_FIELD(SplineFollowerComponent, finished_event), "完成通知",
                    {.required = false})
                .build());

        add(registry,
            describe_component<WeatherComponent>("weather", "天气与日照")
                .field(COMET_FIELD(WeatherComponent, enabled), "启用", {.required = false})
                .field(COMET_FIELD(WeatherComponent, hour), "时刻（小时）",
                    {.required = false,
                        .numeric = {.minimum = 0.0f, .maximum = 24.0f, .enforce_bounds = true}})
                .field(COMET_FIELD(WeatherComponent, day_length), "昼夜周期（秒；0 固定）",
                    {.required = false,
                        .numeric = {.minimum = 0.0f, .maximum = 86400.0f, .enforce_bounds = true}})
                .field(COMET_FIELD(WeatherComponent, overcast), "云量",
                    {.required = false,
                        .numeric = {.minimum = 0.0f, .maximum = 1.0f, .enforce_bounds = true}})
                .field(COMET_FIELD(WeatherComponent, wind), "世界风（米／秒）", {.required = false})
                .field(COMET_FIELD(WeatherComponent, fog_density), "雾密度",
                    {.required = false,
                        .numeric = {.minimum = 0.0f, .maximum = 2.0f, .enforce_bounds = true}})
                .field(COMET_FIELD(WeatherComponent, fog_color), "雾颜色", {.required = false})
                .field(COMET_FIELD(WeatherComponent, sun), "太阳实体", {.required = false})
                .field(COMET_FIELD(WeatherComponent, sun_intensity), "日间太阳强度",
                    {.required = false,
                        .numeric = {.minimum = 0.0f, .maximum = 10000.0f, .enforce_bounds = true}})
                .field(COMET_FIELD(WeatherComponent, sun_yaw), "太阳方位角", {.required = false})
                .field(COMET_FIELD(WeatherComponent, precipitation), "雨／雪发射器",
                    {.required = false})
                .field(COMET_FIELD(WeatherComponent, precipitation_amount), "降水强度",
                    {.required = false,
                        .numeric = {.minimum = 0.0f, .maximum = 1.0f, .enforce_bounds = true}})
                .build());

        add(registry,
            describe_component<VegetationInteractorComponent>("vegetation_interactor", "植被交互体")
                .field(COMET_FIELD(VegetationInteractorComponent, radius), "作用半径",
                    {.required = false,
                        .numeric = {.minimum = .01, .maximum = 20, .enforce_bounds = true}})
                .field(COMET_FIELD(VegetationInteractorComponent, height), "作用高度",
                    {.required = false,
                        .numeric = {.minimum = .01, .maximum = 100, .enforce_bounds = true}})
                .build());

        add(registry,
            describe_component<WaterSurfaceComponent>("water_surface", "水面波纹")
                .field(COMET_FIELD(WaterSurfaceComponent, enabled), "启用", {.required = false})
                .field(COMET_FIELD(WaterSurfaceComponent, amplitude), "法线波纹强度",
                    {.required = false,
                        .numeric = {.minimum = 0.0f, .maximum = 1.0f, .enforce_bounds = true}})
                .field(COMET_FIELD(WaterSurfaceComponent, wavelength), "波长（米）",
                    {.required = false,
                        .numeric = {.minimum = 0.01f, .maximum = 1000.0f, .enforce_bounds = true}})
                .field(COMET_FIELD(WaterSurfaceComponent, speed), "相位速度",
                    {.required = false,
                        .numeric = {.minimum = -10.0f, .maximum = 10.0f, .enforce_bounds = true}})
                .build());

        add(registry,
            describe_component<WindSwayComponent>("wind_sway", "风动摆动")
                .field(COMET_FIELD(WindSwayComponent, enabled), "启用")
                .field(COMET_FIELD(WindSwayComponent, amplitude), "摆动幅度（度）")
                .field(
                    COMET_FIELD(WindSwayComponent, flexible), "柔性顶点风动", {.required = false})
                .field(COMET_FIELD(WindSwayComponent, height), "弯曲高度（本地米）",
                    {.required = false,
                        .numeric = {.minimum = .01f, .maximum = 1000, .enforce_bounds = true}})
                .field(COMET_FIELD(WindSwayComponent, interaction_radius), "角色交互半径（米）",
                    {.required = false,
                        .numeric = {.minimum = 0, .maximum = 20, .enforce_bounds = true}})
                .field(COMET_FIELD(WindSwayComponent, interaction_strength), "交互弯曲（度）",
                    {.required = false,
                        .numeric = {.minimum = 0, .maximum = 30, .enforce_bounds = true}})
                .field(COMET_FIELD(WindSwayComponent, wind_response), "世界风响应",
                    {.required = false,
                        .numeric = {.minimum = 0, .maximum = 10, .enforce_bounds = true}})
                .field(COMET_FIELD(WindSwayComponent, frequency), "频率（Hz）",
                    {.numeric = {.minimum = .01f, .maximum = 10, .enforce_bounds = true}})
                .build());

        add(registry,
            describe_component<CameraControllerComponent>("camera_controller", "Camera Controller")
                .field(COMET_FIELD(CameraControllerComponent, enabled), "Enabled")
                .enum_field(COMET_FIELD(CameraControllerComponent, mode), "Control Mode",
                    {{CameraControllerComponent::Mode::Free, {"free", "Free"}},
                        {CameraControllerComponent::Mode::Orbit, {"orbit", "Orbit"}}},
                    {.required = false})
                .field(COMET_FIELD(CameraControllerComponent, move_speed), "Move Speed",
                    {.numeric = {.speed = 0.1f, .minimum = 0.0f, .maximum = 1000.0f}})
                .field(COMET_FIELD(CameraControllerComponent, look_sensitivity),
                    "Mouse Look Sensitivity",
                    {.numeric = {.speed = 0.01f, .minimum = 0.0f, .maximum = 10.0f}})
                .field(COMET_FIELD(CameraControllerComponent, look_speed), "Look Speed (deg/s)",
                    {.required = false,
                        .numeric = {.speed = 1.0f, .minimum = 0.0f, .maximum = 720.0f}})
                .field(COMET_FIELD(CameraControllerComponent, orbit_distance), "Orbit Distance",
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0.01f, .enforce_bounds = true}})
                .field(COMET_FIELD(CameraControllerComponent, orbit_height), "Orbit Height",
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(CameraControllerComponent, orbit_min_distance),
                    "Minimum Orbit Distance",
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0.01f, .enforce_bounds = true}})
                .field(COMET_FIELD(CameraControllerComponent, orbit_max_distance),
                    "Maximum Orbit Distance",
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0.01f, .enforce_bounds = true}})
                .field(COMET_FIELD(CameraControllerComponent, orbit_zoom_step), "Orbit Zoom Step",
                    {.required = false,
                        .numeric = {.speed = 0.05f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(CameraControllerComponent, collision_radius),
                    "Camera Collision Radius",
                    {.required = false,
                        .numeric = {.speed = 0.01f, .minimum = 0.01f, .enforce_bounds = true}})
                .build());
        registry.set_reference_filter("stream_cell", [](const void*, AssetReferenceScope scope) {
            return scope != AssetReferenceScope::Resident;
        });
    }
    void ComponentChangeAccess::remove_wind(Scene& scene, Entity entity) {
        scene.set_vertex_bend(entity, {});
    }

}

#include "scene/component_registration.h"
#include "scene/component_changes.h"

namespace Comet {
    void ComponentRegistration::render(ComponentRegistry& registry) {
        add(registry, describe_component<MeshLodComponent>("mesh_lod", "静态网格 LOD")
                          .field(COMET_FIELD(MeshLodComponent, level1), "LOD 1 网格",
                              {.required = false, .asset_type = AssetType::Mesh})
                          .field(COMET_FIELD(MeshLodComponent, level2), "LOD 2 网格",
                              {.required = false, .asset_type = AssetType::Mesh})
                          .field(COMET_FIELD(MeshLodComponent, level1_height), "LOD 1 屏幕高度比例",
                              {.required = false,
                                  .numeric = {.speed = 0.01f,
                                      .minimum = 0.001f,
                                      .maximum = 1,
                                      .enforce_bounds = true}})
                          .field(COMET_FIELD(MeshLodComponent, level2_height), "LOD 2 屏幕高度比例",
                              {.required = false,
                                  .numeric = {.speed = 0.01f,
                                      .minimum = 0.001f,
                                      .maximum = 1,
                                      .enforce_bounds = true}})
                          .field(COMET_FIELD(MeshLodComponent, hysteresis), "切换滞回比例",
                              {.required = false,
                                  .numeric = {.speed = 0.01f,
                                      .minimum = 0,
                                      .maximum = 0.5f,
                                      .enforce_bounds = true}})
                          .enum_field(COMET_FIELD(MeshLodComponent, preview), "层级预览",
                              {{MeshLod::Preview::Auto, {"auto", "自动"}},
                                  {MeshLod::Preview::Original, {"original", "LOD 0（原网格）"}},
                                  {MeshLod::Preview::Level1, {"level1", "LOD 1"}},
                                  {MeshLod::Preview::Level2, {"level2", "LOD 2"}}},
                              {.required = false})
                          .build());

        add(registry, describe_component<MeshRendererComponent>("mesh_renderer", "Mesh Renderer")
                          .field(COMET_FIELD(MeshRendererComponent, mesh), "Mesh",
                              {.asset_type = AssetType::Mesh})
                          .field(COMET_FIELD(MeshRendererComponent, material), "Material",
                              {.asset_type = AssetType::Material})
                          .field(COMET_FIELD(MeshRendererComponent, contributes_to_gi),
                              "参与 GI 几何", {.required = false})
                          .build());

        add(registry,
            describe_component<OutlineComponent>("outline", "描边／遮罩")
                .field(COMET_FIELD(OutlineComponent, enabled), "启用")
                .field(COMET_FIELD(OutlineComponent, color), "颜色")
                .field(COMET_FIELD(OutlineComponent, width), "宽度（输出像素）",
                    {.numeric = {.speed = .1f, .minimum = 0, .maximum = 8, .enforce_bounds = true}})
                .field(COMET_FIELD(OutlineComponent, fill), "填充透明度",
                    {.numeric =
                            {.speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .field(COMET_FIELD(OutlineComponent, through_walls), "穿透遮挡")
                .build());

        add(registry,
            describe_component<CameraComponent>("camera", "Camera")
                .field(COMET_FIELD(CameraComponent, primary), "Primary")
                .enum_field(COMET_FIELD(CameraComponent, projection), "Projection",
                    {{CameraComponent::Projection::Perspective, {"perspective", "Perspective"}},
                        {CameraComponent::Projection::Orthographic,
                            {"orthographic", "Orthographic"}}},
                    {.required = false})
                .field(COMET_FIELD(CameraComponent, fov), "Perspective FOV",
                    {.numeric = {.speed = 1.0f, .minimum = 1.0f, .maximum = 179.0f}})
                .field(COMET_FIELD(CameraComponent, orthographic_height), "Orthographic Height",
                    {.required = false, .numeric = {.speed = 0.1f, .minimum = 0.001f}})
                .field(COMET_FIELD(CameraComponent, near_clip), "Near Clip",
                    {.numeric = {.speed = 0.01f, .minimum = 0.001f}})
                .field(COMET_FIELD(CameraComponent, far_clip), "Far Clip",
                    {.numeric = {.speed = 1.0f, .minimum = 0.001f}})
                .build());

        add(registry,
            describe_component<LightComponent>("light", "Light")
                .enum_field(COMET_FIELD(LightComponent, type), "Type",
                    {{LightType::Directional, {"directional", "Directional"}},
                        {LightType::Point, {"point", "Point"}},
                        {LightType::Spot, {"spot", "Spot"}}})
                .field(COMET_FIELD(LightComponent, enabled), "Enabled")
                .field(
                    COMET_FIELD(LightComponent, casts_shadow), "Cast shadow", {.required = false})
                .field(COMET_FIELD(LightComponent, shadow_distance), "方向光阴影距离（米）",
                    {.required = false,
                        .numeric =
                            {.speed = 1, .minimum = 1, .maximum = 10000, .enforce_bounds = true}})
                .field(COMET_FIELD(LightComponent, color), "Color (linear)",
                    {.numeric = {.speed = 0.01f, .minimum = 0.0f, .maximum = 1.0f}})
                .field(COMET_FIELD(LightComponent, intensity), "Intensity",
                    {.numeric = {.speed = 0.1f, .minimum = 0.0f, .maximum = 10000.0f}})
                .field(COMET_FIELD(LightComponent, range), "Range",
                    {.numeric = {.speed = 0.1f, .minimum = 0.001f, .maximum = 1000000.0f}})
                .field(COMET_FIELD(LightComponent, inner_angle), "Inner angle",
                    {.numeric = {.speed = 0.5f, .minimum = 0.0f, .maximum = 89.0f}})
                .field(COMET_FIELD(LightComponent, outer_angle), "Outer angle",
                    {.numeric = {.speed = 0.5f, .minimum = 0.01f, .maximum = 89.0f}})
                .build());

        add(registry,
            describe_component<DecalComponent>("decal", "贴花")
                .field(COMET_FIELD(DecalComponent, enabled), "启用", {.required = false})
                .field(COMET_FIELD(DecalComponent, texture), "颜色贴图",
                    {.required = false, .asset_type = AssetType::Texture})
                .field(COMET_FIELD(DecalComponent, half_extents), "半尺寸（本地米）",
                    {.required = false,
                        .numeric = {.speed = .05f,
                            .minimum = .01f,
                            .maximum = 1000,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(DecalComponent, color), "颜色／不透明度",
                    {.required = false,
                        .numeric = {.minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .field(COMET_FIELD(DecalComponent, roughness), "粗糙度",
                    {.required = false,
                        .numeric = {.speed = .01f,
                            .minimum = .045f,
                            .maximum = 1,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(DecalComponent, normal_threshold), "表面朝向阈值",
                    {.required = false,
                        .numeric =
                            {.speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .field(COMET_FIELD(DecalComponent, edge_fade), "边缘淡出",
                    {.required = false,
                        .numeric =
                            {.speed = .01f, .minimum = 0, .maximum = .5f, .enforce_bounds = true}})
                .field(COMET_FIELD(DecalComponent, draw_order), "覆盖顺序",
                    {.required = false,
                        .numeric =
                            {.speed = 1, .minimum = 0, .maximum = 1000, .enforce_bounds = true}})
                .build());

        add(registry,
            describe_component<ReflectionProbeComponent>("reflection_probe", "局部反射探针")
                .field(COMET_FIELD(ReflectionProbeComponent, enabled), "启用")
                .field(COMET_FIELD(ReflectionProbeComponent, environment), "环境贴图",
                    {.asset_type = AssetType::Environment})
                .field(COMET_FIELD(ReflectionProbeComponent, half_extents), "影响范围半尺寸（米）",
                    {.numeric = {.speed = .1f,
                         .minimum = .01f,
                         .maximum = 100000,
                         .enforce_bounds = true}})
                .field(COMET_FIELD(ReflectionProbeComponent, blend_distance), "边缘过渡（米）",
                    {.numeric = {.speed = .1f,
                         .minimum = 0,
                         .maximum = 100000,
                         .enforce_bounds = true}})
                .field(COMET_FIELD(ReflectionProbeComponent, intensity), "反射强度",
                    {.numeric =
                            {.speed = .05f, .minimum = 0, .maximum = 64, .enforce_bounds = true}})
                .field(COMET_FIELD(ReflectionProbeComponent, rotation), "贴图旋转（度）")
                .field(COMET_FIELD(ReflectionProbeComponent, box_projection), "盒投影校正")
                .field(COMET_FIELD(ReflectionProbeComponent, capture_resolution), "烘焙面尺寸",
                    {.required = false,
                        .numeric =
                            {.speed = 1, .minimum = 16, .maximum = 512, .enforce_bounds = true}})
                .field(COMET_FIELD(ReflectionProbeComponent, capture_distance), "烘焙距离（米）",
                    {.required = false,
                        .numeric = {.speed = 1,
                            .minimum = 0.1f,
                            .maximum = 100000,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(ReflectionProbeComponent, bake_fingerprint), "烘焙输入",
                    {.editable = false, .required = false})
                .build());

        add(registry,
            describe_component<IrradianceVolumeComponent>("irradiance_volume", "探针 GI 区域")
                .field(COMET_FIELD(IrradianceVolumeComponent, enabled), "启用")
                .field(COMET_FIELD(IrradianceVolumeComponent, realtime), "实时更新",
                    {.required = false})
                .field(COMET_FIELD(IrradianceVolumeComponent, update_budget), "区域每帧上限",
                    {.required = false,
                        .numeric =
                            {.speed = 1, .minimum = 1, .maximum = 4096, .enforce_bounds = true}})
                .field(COMET_FIELD(IrradianceVolumeComponent, update_rays), "每次更新光线数",
                    {.required = false,
                        .numeric =
                            {.speed = 16, .minimum = 64, .maximum = 256, .enforce_bounds = true}})
                .field(COMET_FIELD(IrradianceVolumeComponent, bounce_strength),
                    "实时多跳反馈（0 关闭）",
                    {.required = false,
                        .numeric = {.minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .field(COMET_FIELD(IrradianceVolumeComponent, history_weight), "历史权重",
                    {.required = false,
                        .numeric =
                            {.speed = .01f, .minimum = 0, .maximum = .99f, .enforce_bounds = true}})
                .field(COMET_FIELD(IrradianceVolumeComponent, irradiance), "烘焙光照",
                    {.asset_type = AssetType::Irradiance})
                .field(COMET_FIELD(IrradianceVolumeComponent, half_extents), "区域半尺寸（米）",
                    {.numeric = {.speed = .1f,
                         .minimum = .01f,
                         .maximum = 100000,
                         .enforce_bounds = true}})
                .field(COMET_FIELD(IrradianceVolumeComponent, spacing), "探针间距（米）",
                    {.numeric = {.speed = .1f,
                         .minimum = .01f,
                         .maximum = 10000,
                         .enforce_bounds = true}})
                .field(COMET_FIELD(IrradianceVolumeComponent, max_distance), "烘焙距离（米）",
                    {.numeric = {.speed = 1,
                         .minimum = .1f,
                         .maximum = 100000,
                         .enforce_bounds = true}})
                .field(COMET_FIELD(IrradianceVolumeComponent, ray_samples), "每探针光线数",
                    {.numeric =
                            {.speed = 32, .minimum = 32, .maximum = 8192, .enforce_bounds = true}})
                .field(COMET_FIELD(IrradianceVolumeComponent, bounces), "漫反射反弹数",
                    {.numeric = {.speed = 1, .minimum = 0, .maximum = 4, .enforce_bounds = true}})
                .field(COMET_FIELD(IrradianceVolumeComponent, intensity), "间接光强度",
                    {.numeric =
                            {.speed = .05f, .minimum = 0, .maximum = 64, .enforce_bounds = true}})
                .field(COMET_FIELD(IrradianceVolumeComponent, normal_bias), "采样偏移（米）",
                    {.numeric =
                            {.speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .field(COMET_FIELD(IrradianceVolumeComponent, blend_distance), "混合距离（米）",
                    {.required = false,
                        .numeric = {.speed = .1f,
                            .minimum = 0,
                            .maximum = 100000,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(IrradianceVolumeComponent, bake_fingerprint), "烘焙输入",
                    {.editable = false, .required = false})
                .build());
        registry.add_scene_references(
            [](Scene& scene, AssetReferenceScope scope, auto& references) {
                const auto& environment = scene.get_environment();
                if(environment.asset
                    && (scope == AssetReferenceScope::All || environment.background
                        || environment.lighting))
                    references.push_back({environment.asset, AssetType::Environment, false});
            });
        registry.set_reference_filter(
            "reflection_probe", [](const void* data, AssetReferenceScope scope) {
                const auto& value = *static_cast<const ReflectionProbeComponent*>(data);
                return scope == AssetReferenceScope::All || value.enabled;
            });

        registry.set_reference_filter(
            "irradiance_volume", [](const void* data, AssetReferenceScope scope) {
                const auto& value = *static_cast<const IrradianceVolumeComponent*>(data);
                return scope == AssetReferenceScope::All || (value.enabled && !value.realtime);
            });

        registry.set_reference_filter("decal", [](const void* data, AssetReferenceScope scope) {
            const auto& value = *static_cast<const DecalComponent*>(data);
            return scope == AssetReferenceScope::All || value.enabled;
        });
    }
    void ComponentChangeAccess::render(Scene& scene, Entity entity, bool) {
        scene.mark_render_entity_changed(entity.m_handle);
    }
    void ComponentChangeAccess::mesh(Scene& scene, Entity entity, bool structure) {
        render(scene, entity, structure);
        if(structure)
            scene.m_render_structure_changed = true;
    }
    void ComponentChangeAccess::replace_mesh(
        Scene& scene, Entity entity, const void* previous, const void* next) {
        if(static_cast<const MeshRendererComponent*>(previous)->material
            != static_cast<const MeshRendererComponent*>(next)->material)
            scene.clear_material_overrides(scene.m_registry, entity.m_handle);
    }

}

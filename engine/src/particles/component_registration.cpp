#include "scene/component_registration.h"
#include "scene/component_changes.h"

namespace Comet {
    void ComponentRegistration::particles(ComponentRegistry& registry) {
        add(registry,
            describe_component<ParticleMeshCollisionComponent>(
                "particle_mesh_collision", "粒子网格碰撞")
                .field(COMET_FIELD(ParticleMeshCollisionComponent, enabled), "启用",
                    {.required = false})
                .field(COMET_FIELD(ParticleMeshCollisionComponent, mesh), "碰撞网格（可选）",
                    {.required = false, .asset_type = AssetType::Mesh, .asset_cpu_only = true})
                .field(COMET_FIELD(ParticleMeshCollisionComponent, layers), "粒子碰撞层",
                    {.required = false, .bitmask = true})
                .field(COMET_FIELD(ParticleMeshCollisionComponent, deformed), "使用蒙皮与柔性姿态",
                    {.required = false})
                .build());

        add(registry,
            describe_component<ParticleEmitterComponent>("particle_emitter", "Particle Emitter")
                .enum_field(COMET_FIELD(ParticleEmitterComponent, simulation), "模拟方式",
                    {{ParticleSimulation::CPU, {"cpu", "CPU（支持物理碰撞）"}},
                        {ParticleSimulation::GPU, {"gpu", "GPU（大规模视觉粒子）"}}},
                    {.required = false})
                .field(COMET_FIELD(ParticleEmitterComponent, gpu_dynamics),
                    "GPU 有状态模拟／深度碰撞", {.required = false})
                .field(COMET_FIELD(ParticleEmitterComponent, gpu_world_collision),
                    "GPU 世界碰撞（基础形状／静态网格）", {.required = false})
                .field(COMET_FIELD(ParticleEmitterComponent, mesh_collision_budget),
                    "网格碰撞查询预算",
                    {.required = false,
                        .numeric = {.minimum = 16, .maximum = 2048, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, vector_field), "GPU 三维向量场",
                    {.required = false, .asset_type = AssetType::VectorField})
                .field(COMET_FIELD(ParticleEmitterComponent, vector_field_strength), "向量场强度",
                    {.required = false,
                        .numeric = {.minimum = -1000, .maximum = 1000, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, wind), "GPU 风加速度（世界空间）",
                    {.required = false})
                .field(COMET_FIELD(ParticleEmitterComponent, vortex_center),
                    "GPU 涡旋中心（世界空间）", {.required = false})
                .field(COMET_FIELD(ParticleEmitterComponent, vortex_axis), "GPU 涡旋轴",
                    {.required = false})
                .field(COMET_FIELD(ParticleEmitterComponent, vortex_strength), "GPU 涡旋加速度",
                    {.required = false,
                        .numeric = {.minimum = -1000, .maximum = 1000, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, vortex_radius), "GPU 涡旋半径（米）",
                    {.required = false,
                        .numeric = {.minimum = .01f, .maximum = 100000, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, turbulence), "GPU 扰动加速度",
                    {.required = false,
                        .numeric =
                            {.speed = .1f, .minimum = 0, .maximum = 1000, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, turbulence_frequency), "GPU 扰动频率",
                    {.required = false,
                        .numeric = {.speed = .1f,
                            .minimum = .01f,
                            .maximum = 100,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, texture), "Particle Texture",
                    {.required = false, .asset_type = AssetType::Texture})
                .enum_field(COMET_FIELD(ParticleEmitterComponent, blend), "Blend",
                    {{ParticleBlend::Additive, {"additive", "加法（火花／发光）"}},
                        {ParticleBlend::Alpha, {"alpha", "Alpha（烟雾／半透明）"}}},
                    {.required = false})
                .field(COMET_FIELD(ParticleEmitterComponent, soft_distance),
                    "交界淡出距离（米，0 关闭）",
                    {.required = false,
                        .numeric = {.speed = 0.01f,
                            .minimum = 0,
                            .maximum = 1000,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, atlas_columns), "图集列数",
                    {.required = false,
                        .numeric =
                            {.speed = 0.1f, .minimum = 1, .maximum = 64, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, atlas_rows), "图集行数",
                    {.required = false,
                        .numeric =
                            {.speed = 0.1f, .minimum = 1, .maximum = 64, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, frame_count), "有效帧数",
                    {.required = false,
                        .numeric =
                            {.speed = 0.1f, .minimum = 1, .maximum = 4096, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, frame_rate), "帧率（0 按寿命播放）",
                    {.required = false,
                        .numeric =
                            {.speed = 0.1f, .minimum = 0, .maximum = 240, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, loop_animation), "循环序列帧",
                    {.required = false})
                .enum_field(COMET_FIELD(ParticleEmitterComponent, collision), "粒子碰撞",
                    {{ParticleCollision::None, {"none", "关闭"}},
                        {ParticleCollision::Bounce, {"bounce", "反弹"}},
                        {ParticleCollision::Kill, {"kill", "碰撞后消失"}}},
                    {.required = false})
                .field(COMET_FIELD(ParticleEmitterComponent, collision_radius),
                    "碰撞半径（米，0 使用射线）",
                    {.required = false,
                        .numeric =
                            {.speed = 0.01f, .minimum = 0, .maximum = 10, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, restitution), "碰撞弹性",
                    {.required = false,
                        .numeric =
                            {.speed = 0.01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, friction), "碰撞摩擦",
                    {.required = false,
                        .numeric =
                            {.speed = 0.01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, collision_layers), "碰撞层掩码",
                    {.required = false, .bitmask = true})
                .field(
                    COMET_FIELD(ParticleEmitterComponent, enabled), "Enabled", {.required = false})
                .field(COMET_FIELD(ParticleEmitterComponent, capacity), "Capacity",
                    {.required = false,
                        .numeric = {.speed = 0.1f,
                            .minimum = 1,
                            .maximum = 65536,
                            .enforce_bounds = true}})
                .field(
                    COMET_FIELD(ParticleEmitterComponent, seed), "Random Seed", {.required = false})
                .field(COMET_FIELD(ParticleEmitterComponent, burst), "Startup Burst",
                    {.required = false,
                        .numeric = {.speed = 0.1f,
                            .minimum = 0,
                            .maximum = 65536,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, rate), "Emission Rate",
                    {.required = false,
                        .numeric = {.speed = 0.1f,
                            .minimum = 0,
                            .maximum = 1000000,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, lifetime), "Lifetime",
                    {.required = false,
                        .numeric = {.speed = 0.1f,
                            .minimum = 0.01,
                            .maximum = 60,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, speed), "Speed",
                    {.required = false,
                        .numeric =
                            {.speed = 0.1f, .minimum = 0, .maximum = 1000, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, spread), "Cone Angle",
                    {.required = false,
                        .numeric =
                            {.speed = 0.1f, .minimum = 0, .maximum = 180, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, acceleration), "Acceleration",
                    {.required = false})
                .field(COMET_FIELD(ParticleEmitterComponent, start_color), "Start Color",
                    {.required = false})
                .field(COMET_FIELD(ParticleEmitterComponent, end_color), "End Color",
                    {.required = false})
                .field(COMET_FIELD(ParticleEmitterComponent, spawn_extents), "发射盒半尺寸",
                    {.required = false,
                        .numeric =
                            {.speed = .1f, .minimum = 0, .maximum = 10000, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, start_size), "Start Size",
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(ParticleEmitterComponent, end_size), "End Size",
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}})
                .build());

        add(registry,
            describe_component<TrailComponent>("trail", "拖尾")
                .field(COMET_FIELD(TrailComponent, enabled), "启用", {.required = false})
                .field(COMET_FIELD(TrailComponent, texture), "拖尾纹理",
                    {.required = false, .asset_type = AssetType::Texture})
                .enum_field(COMET_FIELD(TrailComponent, blend), "混合方式",
                    {{ParticleBlend::Additive, {"additive", "加法"}},
                        {ParticleBlend::Alpha, {"alpha", "Alpha"}}},
                    {.required = false})
                .field(COMET_FIELD(TrailComponent, capacity), "最多采样点",
                    {.required = false,
                        .numeric =
                            {.speed = 1, .minimum = 2, .maximum = 512, .enforce_bounds = true}})
                .field(COMET_FIELD(TrailComponent, lifetime), "保留时间（秒）",
                    {.required = false,
                        .numeric = {.speed = 0.01f,
                            .minimum = 0.01f,
                            .maximum = 60,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(TrailComponent, minimum_distance), "采样间距（米）",
                    {.required = false,
                        .numeric = {.speed = 0.01f, .minimum = 0.001f, .enforce_bounds = true}})
                .field(COMET_FIELD(TrailComponent, break_distance), "瞬移断开距离（米）",
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0.01f, .enforce_bounds = true}})
                .field(COMET_FIELD(TrailComponent, width), "宽度（米）",
                    {.required = false,
                        .numeric = {.speed = 0.01f, .minimum = 0.001f, .enforce_bounds = true}})
                .field(COMET_FIELD(TrailComponent, color), "颜色", {.required = false})
                .field(COMET_FIELD(TrailComponent, soft_distance), "交界淡出距离（米）",
                    {.required = false,
                        .numeric = {.speed = 0.01f,
                            .minimum = 0,
                            .maximum = 1000,
                            .enforce_bounds = true}})
                .build());
        registry.add_scene_references(
            [](Scene& scene, AssetReferenceScope scope, auto& references) {
                if(scope == AssetReferenceScope::All || !scene.get_particles())
                    return;
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
            });
    }
    void ComponentChangeAccess::particles(Scene& scene, Entity entity, bool structure) {
        render(scene, entity, structure);
        ++scene.m_particle_revision;
    }

}

#include "gameplay/world_effects.h"
#include "scene/scene.h"
#include "scene/material_parameters.h"
#include <algorithm>
#include <cmath>

namespace Comet::Gameplay {
    namespace {
        bool set_world_pose(
            Scene& scene, Entity entity, Math::Vec3 position, std::optional<Math::Quat> rotation) {
            auto transform = entity.get_component<TransformComponent>();
            transform.translation = Math::Vec3(
                Math::inverse(scene.get_parent_matrix(entity)) * Math::Vec4(position, 1));
            if(rotation) {
                Math::Quat parent(1, 0, 0, 0);
                if(auto ancestor = scene.get_parent(entity))
                    parent = Math::to_quat(
                        ancestor.get_component<WorldTransformComponent>().pose_world_matrix);
                transform.rotation =
                    Math::degrees(Math::euler_angles(Math::inverse(parent) * *rotation));
            }
            return entity.try_set_transform(transform);
        }
    }
    void SplineMotionSystem::release_camera(Scene& scene, EntityId id, State& state) {
        if(state.had_controller)
            if(auto entity = scene.find_entity(id);
                entity && entity.has_component<CameraControllerComponent>())
                entity.get_component<CameraControllerComponent>().enabled =
                    state.controller_enabled;
        state.had_controller = state.active = false;
    }
    Result<void, Error> SplineMotionSystem::on_start(
        Scene& scene, RuntimeSession&, const RuntimeServices&) {
        m_states.clear();
        return advance(scene, 0, nullptr, Stage::Start);
    }
    Result<void, Error> SplineMotionSystem::update(Scene& scene, const Context& context) {
        return advance(scene, context.delta_time, &context.input, Stage::Update);
    }
    Result<void, Error> SplineMotionSystem::fixed_update(Scene& scene, const Context& context) {
        return advance(scene, context.delta_time, &context.input, Stage::Fixed);
    }
    Result<void, Error> SplineMotionSystem::advance(
        Scene& scene, double delta, const InputState* input, Stage stage) {
        auto result = Result<void, Error>::success();
        scene.update_world_transforms();
        scene.each<const SplineFollowerComponent>([&](Entity entity, const auto& follower) {
            if(!result)
                return;
            const bool physical = entity.has_component<RigidBodyComponent>();
            if((stage == Stage::Fixed && !physical) || (stage == Stage::Update && physical))
                return;
            if(entity.has_component<CharacterControllerComponent>()
                || (physical
                    && entity.get_component<RigidBodyComponent>().motion
                           != BodyMotion::Kinematic)) {
                result = Result<void, Error>::failure({"样条运动只驱动普通实体或运动学刚体"});
                return;
            }
            auto& state = m_states[entity.get_id()];
            if(!follower.playing || !follower.path) {
                release_camera(scene, entity.get_id(), state);
                return;
            }
            Entity path_entity = scene.find_entity(follower.path);
            if(!path_entity || !path_entity.has_component<SplineComponent>()
                || !std::isfinite(follower.duration) || follower.duration < .01f
                || follower.duration > 86400) {
                result =
                    Result<void, Error>::failure({"样条跟随需要有效路径与 0.01～86400 秒时长"});
                return;
            }
            for(auto ancestor = path_entity; ancestor; ancestor = scene.get_parent(ancestor))
                if(ancestor == entity) {
                    result = Result<void, Error>::failure({"样条路径不能位于跟随者自身或子树中"});
                    return;
                }
            const auto& path = path_entity.get_component<SplineComponent>();
            if(!Math::is_finite(path.start) || !Math::is_finite(path.control_a)
                || !Math::is_finite(path.control_b) || !Math::is_finite(path.end)) {
                result = Result<void, Error>::failure({"样条控制点必须有限"});
                return;
            }
            if(!state.active || state.path != follower.path) {
                release_camera(scene, entity.get_id(), state);
                state.elapsed = 0;
                state.path = follower.path;
                state.active = true;
                if(entity.has_component<CameraControllerComponent>()) {
                    state.had_controller = true;
                    state.controller_enabled =
                        entity.get_component<CameraControllerComponent>().enabled;
                    entity.get_component<CameraControllerComponent>().enabled = false;
                }
            }
            state.elapsed += delta;
            bool skip = false;
            if(input && !follower.skip_action.empty()) {
                const auto action = input->read_action(
                    follower.skip_action, InputState::Action::Type::Button, false);
                if(!action) {
                    result = Result<void, Error>::failure({action.error()});
                    return;
                }
                skip = action.value().pressed;
            }
            const bool finished = skip || (!follower.loop && state.elapsed >= follower.duration);
            float t = finished ? 1.f : float(std::fmod(state.elapsed / follower.duration, 1.0));
            if(follower.ease)
                t = t * t * (3 - 2 * t);
            const auto matrix = scene.get_world_matrix(path_entity);
            const auto position = Math::Vec3(matrix * Math::Vec4(path.sample(t), 1));
            std::optional<Math::Quat> rotation;
            if(follower.orient) {
                auto direction = Math::Vec3(matrix * Math::Vec4(path.tangent(t), 0));
                if(auto forward = Math::normalized_direction(direction)) {
                    const auto up =
                        std::abs(forward->y) > .99f ? Math::Vec3(0, 0, 1) : Math::Vec3(0, 1, 0);
                    rotation = Math::to_quat(
                        Math::inverse(Math::look_at(position, position + *forward, up)));
                }
            }
            if(!set_world_pose(scene, entity, position, rotation)) {
                result = Result<void, Error>::failure({"样条跟随无法转换到实体父空间"});
                return;
            }
            if(finished) {
                entity.edit_component<SplineFollowerComponent>(
                    [](auto& value) { value.playing = false; });
                release_camera(scene, entity.get_id(), state);
                if(!follower.finished_event.empty()
                    && !scene.emit_event(follower.finished_event, {}, entity))
                    result = Result<void, Error>::failure({"无法提交样条完成事件"});
            }
        });
        for(auto iterator = m_states.begin(); iterator != m_states.end();) {
            auto entity = scene.find_entity(iterator->first);
            if(entity && entity.has_component<SplineFollowerComponent>()) {
                ++iterator;
                continue;
            }
            release_camera(scene, iterator->first, iterator->second);
            iterator = m_states.erase(iterator);
        }
        return result;
    }
    void SplineMotionSystem::on_stop(
        Scene& scene, RuntimeSession&, const RuntimeServices&) noexcept {
        for(auto& [id, state] : m_states)
            release_camera(scene, id, state);
        m_states.clear();
    }
    Result<void, Error> WeatherSystem::on_start(
        Scene& scene, RuntimeSession&, const RuntimeServices&) {
        return apply(scene, 0);
    }
    Result<void, Error> WeatherSystem::update(Scene& scene, const Context& context) {
        return apply(scene, context.total_time);
    }
    void WeatherSystem::restore_sun(Scene& scene, EntityId id, const Sun& saved) {
        if(auto entity = scene.find_entity(id); entity && entity.has_component<LightComponent>()) {
            entity.edit_component<LightComponent>(
                [&](auto& value) { value.intensity = saved.intensity; });
            entity.edit_transform([&](auto& value) { value.rotation = saved.rotation; });
        }
    }
    void WeatherSystem::restore_precipitation(
        Scene& scene, EntityId id, const Precipitation& saved) {
        if(auto entity = scene.find_entity(id);
            entity && entity.has_component<ParticleEmitterComponent>())
            entity.edit_component<ParticleEmitterComponent>([&](auto& value) {
                value.enabled = saved.enabled;
                value.rate = saved.rate;
                value.wind = saved.wind;
            });
    }
    void WeatherSystem::restore(Scene& scene) noexcept {
        if(m_environment) {
            auto environment = scene.get_environment();
            environment.wind = m_environment->wind;
            environment.lighting_intensity = m_environment->lighting_intensity;
            environment.volumetric_fog.enabled = m_environment->volumetric_fog.enabled;
            environment.volumetric_fog.density = m_environment->volumetric_fog.density;
            environment.volumetric_fog.color = m_environment->volumetric_fog.color;
            static_cast<void>(scene.set_environment(environment));
        }
        m_environment.reset();
        for(const auto& [id, state] : m_suns)
            restore_sun(scene, id, state);
        for(const auto& [id, state] : m_precipitation)
            restore_precipitation(scene, id, state);
        m_suns.clear();
        m_precipitation.clear();
    }
    Result<void, Error> WeatherSystem::apply(Scene& scene, double time) {
        std::optional<WeatherComponent> weather;
        bool duplicate = false;
        scene.each<const WeatherComponent>([&](Entity, const auto& value) {
            if(!value.enabled)
                return;
            if(weather)
                duplicate = true;
            weather = value;
        });
        if(duplicate)
            return Result<void, Error>::failure({"场景只允许一个启用的天气控制器"});
        if(!weather) {
            restore(scene);
            return Result<void, Error>::success();
        }
        const auto& value = *weather;
        if(!std::isfinite(value.hour) || value.hour < 0 || value.hour > 24
            || !std::isfinite(value.day_length) || value.day_length < 0
            || !std::isfinite(value.overcast) || value.overcast < 0 || value.overcast > 1
            || !Math::is_finite(value.wind) || Math::length(value.wind) > 100
            || !std::isfinite(value.fog_density) || value.fog_density < 0 || value.fog_density > 2
            || !Math::is_finite(value.fog_color) || Math::any_less(value.fog_color, Math::Vec3(0))
            || Math::any_greater(value.fog_color, Math::Vec3(1))
            || !std::isfinite(value.sun_intensity) || value.sun_intensity < 0
            || value.sun_intensity > 10000 || !std::isfinite(value.sun_yaw)
            || !std::isfinite(value.precipitation_amount) || value.precipitation_amount < 0
            || value.precipitation_amount > 1)
            return Result<void, Error>::failure({"天气参数包含无效时刻、风、雾或降水强度"});
        if(!m_environment)
            m_environment = scene.get_environment();
        const float hour = float(
            std::fmod(value.hour + (value.day_length > 0 ? time * 24 / value.day_length : 0), 24));
        const float angle = (hour - 6) * 15;
        const float daylight = std::clamp(std::sin(Math::radians(angle)) * 5, 0.f, 1.f);
        auto environment = scene.get_environment();
        environment.wind = value.wind;
        environment.lighting_intensity = m_environment->lighting_intensity
                                         * (.02f + .98f * daylight) * (1 - value.overcast * .5f);
        environment.volumetric_fog.enabled = value.fog_density > 0;
        environment.volumetric_fog.density = value.fog_density;
        environment.volumetric_fog.color = value.fog_color;
        if(!scene.set_environment(environment))
            return Result<void, Error>::failure({"无法应用天气环境"});
        for(auto it = m_suns.begin(); it != m_suns.end();) {
            auto entity = scene.find_entity(it->first);
            if(entity && entity.get_uuid() == value.sun) {
                ++it;
                continue;
            }
            restore_sun(scene, it->first, it->second);
            it = m_suns.erase(it);
        }
        for(auto it = m_precipitation.begin(); it != m_precipitation.end();) {
            auto entity = scene.find_entity(it->first);
            if(entity && entity.get_uuid() == value.precipitation) {
                ++it;
                continue;
            }
            restore_precipitation(scene, it->first, it->second);
            it = m_precipitation.erase(it);
        }
        if(auto sun = scene.find_entity(value.sun); sun && sun.has_component<LightComponent>()) {
            m_suns.try_emplace(sun.get_id(), Sun{sun.get_component<LightComponent>().intensity,
                                                 sun.get_component<TransformComponent>().rotation});
            sun.edit_component<LightComponent>([&](auto& light) {
                light.intensity = value.sun_intensity * daylight * (1 - value.overcast * .7f);
            });
            auto transform = sun.get_component<TransformComponent>();
            transform.rotation = {-angle, value.sun_yaw, 0};
            if(!sun.try_set_transform(transform))
                return Result<void, Error>::failure({"无法应用太阳方向"});
        }
        if(auto precipitation = scene.find_entity(value.precipitation);
            precipitation && precipitation.has_component<ParticleEmitterComponent>()) {
            const auto& emitter = precipitation.get_component<ParticleEmitterComponent>();
            const auto [source, added] = m_precipitation.try_emplace(
                precipitation.get_id(), Precipitation{emitter.enabled, emitter.rate, emitter.wind});
            precipitation.edit_component<ParticleEmitterComponent>([&](auto& emitter) {
                emitter.enabled = value.precipitation_amount > 0;
                emitter.rate = source->second.rate * value.precipitation_amount;
                if(emitter.gpu_dynamics)
                    emitter.wind = source->second.wind + value.wind;
            });
        }
        return Result<void, Error>::success();
    }
    void WeatherSystem::on_stop(Scene& scene, RuntimeSession&, const RuntimeServices&) noexcept {
        restore(scene);
    }
    Result<void, Error> WaterSurfaceSystem::on_start(
        Scene& scene, RuntimeSession&, const RuntimeServices&) {
        m_active.clear();
        return apply(scene, 0);
    }
    Result<void, Error> WaterSurfaceSystem::update(Scene& scene, const Context& context) {
        return apply(scene, context.total_time);
    }
    Result<void, Error> WaterSurfaceSystem::apply(Scene& scene, double time) {
        auto result = Result<void, Error>::success();
        m_next.clear();
        scene.each<const WaterSurfaceComponent, const MeshRendererComponent>(
            [&](Entity entity, const auto& water, const auto&) {
                if(!result)
                    return;
                if(!std::isfinite(water.amplitude) || water.amplitude < 0 || water.amplitude > 1
                    || !std::isfinite(water.wavelength) || water.wavelength < .01f
                    || water.wavelength > 1000 || !std::isfinite(water.speed)
                    || std::abs(water.speed) > 10) {
                    result = Result<void, Error>::failure({"水面波纹参数无效"});
                    return;
                }
                m_next.push_back(entity.get_id());
                Math::Vec4 parameters(0);
                if(water.enabled && water.amplitude > 0)
                    parameters = {water.amplitude, 2 * Math::PI / water.wavelength,
                        float(std::fmod(time * water.speed, 20 * Math::PI)), 1};
                auto applied =
                    scene.set_material_vector(entity, "water_parameters", parameters, m_materials);
                if(!applied)
                    result = Result<void, Error>::failure({applied.error()});
            });
        for(const auto id : m_active)
            if(auto entity = scene.find_entity(id);
                entity
                && (!entity.has_component<WaterSurfaceComponent>()
                    || !entity.has_component<MeshRendererComponent>()))
                scene.clear_material_parameter(entity, "water_parameters");
        m_active.swap(m_next);
        return result;
    }
}

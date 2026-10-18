#include "gameplay/wind_sway.h"
#include "scene/scene.h"
#include <cmath>
namespace Comet::Gameplay {
    Result<void, Error> WindSwaySystem::update(Scene& scene, const Context& context) {
        m_time += context.delta_time;
        auto result = Result<void, Error>::success();
        struct Actor {
            Math::Vec3 position;
            float radius, height;
        };
        std::vector<Actor> actors;
        scene.update_world_transforms();
        scene.each<const CharacterControllerComponent>([&](Entity entity, const auto& character) {
            actors.push_back({Math::Vec3(scene.get_world_matrix(entity)[3]), character.radius,
                character.height});
        });
        scene.each<const VegetationInteractorComponent>([&](Entity entity, const auto& actor) {
            if(std::isfinite(actor.radius) && actor.radius > 0 && actor.radius <= 20
                && std::isfinite(actor.height) && actor.height > 0 && actor.height <= 100)
                actors.push_back(
                    {Math::Vec3(scene.get_world_matrix(entity)[3]), actor.radius, actor.height});
        });
        scene.each<const WindSwayComponent, const TransformComponent>([&](Entity entity,
                                                                          const auto& wind,
                                                                          const auto& transform) {
            if(!result)
                return;
            if(!wind.enabled || wind.flexible) {
                if(!wind.enabled)
                    scene.set_vertex_bend(entity, {});
                const auto previous = m_states.find(entity.get_id());
                if(previous != m_states.end()) {
                    if(transform.rotation == previous->second.output)
                        entity.edit_transform(
                            [&](auto& value) { value.rotation = previous->second.base; });
                    m_states.erase(previous);
                }
                if(!wind.enabled) {
                    m_interactions.erase(entity.get_id());
                    return;
                }
            }
            if(!Math::is_finite(wind.amplitude) || !std::isfinite(wind.frequency)
                || wind.frequency <= 0 || wind.frequency > 10 || !std::isfinite(wind.height)
                || wind.height <= 0 || wind.height > 1000 || !std::isfinite(wind.interaction_radius)
                || wind.interaction_radius < 0 || wind.interaction_radius > 20
                || !std::isfinite(wind.interaction_strength) || wind.interaction_strength < 0
                || wind.interaction_strength > 30 || !std::isfinite(wind.wind_response)
                || wind.wind_response < 0 || wind.wind_response > 10
                || Math::any_greater(Math::abs(wind.amplitude), Math::Vec3(30))) {
                result = Result<void, Error>::failure({"Invalid wind sway settings"});
                return;
            }
            if(entity.has_component<RigidBodyComponent>()
                || entity.has_component<CharacterControllerComponent>()) {
                result =
                    Result<void, Error>::failure({"Wind sway is only for decorative entities"});
                return;
            }
            const float phase = float(entity.get_id() % 1024) * 2.39996323f;
            const float wave =
                std::sin(float(std::fmod(m_time * wind.frequency, 1.0)) * (Math::PI * 2) + phase);
            if(wind.flexible) {
                const auto matrix = scene.get_world_matrix(entity);
                const bool needs_inverse =
                    wind.interaction_radius > 0 || scene.get_environment().wind != Math::Vec3(0);
                const auto inverse = needs_inverse ? Math::inverse(matrix) : Math::Mat4(1);
                if(!Math::is_finite(inverse)) {
                    result = Result<void, Error>::failure({"风动需要非退化的变换"});
                    return;
                }
                Math::Vec2 target(0);
                if(wind.interaction_radius > 0) {
                    const auto base = Math::Vec3(matrix[3]);
                    for(const auto& actor : actors) {
                        if(actor.position.y > base.y + wind.height
                            || actor.position.y + actor.height < base.y)
                            continue;
                        Math::Vec3 delta = base - actor.position;
                        delta.y = 0;
                        const float distance = Math::length(delta);
                        const float radius = wind.interaction_radius + actor.radius;
                        if(distance >= radius || distance < 1e-5f)
                            continue;
                        const auto local = Math::Vec3(inverse * Math::Vec4(delta / distance, 0));
                        const float weight = 1 - distance / radius;
                        target += Math::Vec2(local.x, local.z)
                                  * std::tan(Math::radians(wind.interaction_strength)) * weight
                                  * weight;
                    }
                }
                Math::Vec2 interaction(0);
                if(wind.interaction_radius > 0) {
                    auto& state = m_interactions[entity.get_id()];
                    state += (target - state) * float(1 - std::exp(-context.delta_time * 10));
                    interaction = state;
                } else
                    m_interactions.erase(entity.get_id());
                const auto angles = Math::radians(wind.amplitude * wave);
                Math::Vec2 bend = Math::Vec2(std::tan(angles.z), -std::tan(angles.x)) + interaction;
                const auto global =
                    Math::Vec3(inverse * Math::Vec4(scene.get_environment().wind, 0));
                bend += Math::Vec2(global.x, global.z) * wind.wind_response * .02f;
                const float magnitude = Math::length(bend);
                if(magnitude > .57735f)
                    bend *= .57735f / magnitude;
                scene.set_vertex_bend(entity, {bend.x, bend.y, wind.height, 0});
                return;
            }
            m_interactions.erase(entity.get_id());
            scene.set_vertex_bend(entity, {});
            auto [it, added] = m_states.try_emplace(
                entity.get_id(), State{transform.rotation, transform.rotation});
            auto& state = it->second;
            if(transform.rotation != state.output)
                state.base = transform.rotation;
            state.output = state.base + wind.amplitude * wave;
            entity.edit_transform([&](auto& value) { value.rotation = state.output; });
        });
        std::erase_if(
            m_interactions, [&](const auto& item) { return !scene.find_entity(item.first); });
        std::erase_if(m_states, [&](const auto& item) {
            Entity entity = scene.find_entity(item.first);
            if(entity && entity.has_component<WindSwayComponent>())
                return false;
            if(entity)
                entity.edit_transform([&](auto& value) {
                    if(value.rotation == item.second.output)
                        value.rotation = item.second.base;
                });
            return true;
        });
        return result;
    }
    void WindSwaySystem::on_stop(Scene& scene, RuntimeSession&, const RuntimeServices&) noexcept {
        scene.each<const WindSwayComponent>(
            [&](Entity entity, const auto&) { scene.set_vertex_bend(entity, {}); });
        for(const auto& [id, state] : m_states)
            if(auto entity = scene.find_entity(id))
                entity.edit_transform([&](auto& value) {
                    if(value.rotation == state.output)
                        value.rotation = state.base;
                });
        m_states.clear();
        m_interactions.clear();
        m_time = 0;
    }
}

#include "gameplay/wind_sway.h"
#include "scene/scene.h"
#include <cmath>
namespace Comet::Gameplay {
    Result<void, Error> WindSwaySystem::update(Scene& scene, const Context& context) {
        m_time += context.delta_time;
        auto result = Result<void, Error>::success();
        scene.each<const WindSwayComponent, const TransformComponent>(
            [&](Entity entity, const auto& wind, const auto& transform) {
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
                    if(!wind.enabled)
                        return;
                }
                if(!Math::is_finite(wind.amplitude) || !std::isfinite(wind.frequency)
                    || wind.frequency <= 0 || wind.frequency > 10 || !std::isfinite(wind.height)
                    || wind.height <= 0 || wind.height > 1000
                    || glm::any(glm::greaterThan(glm::abs(wind.amplitude), Math::Vec3(30)))) {
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
                const float wave = std::sin(
                    float(std::fmod(m_time * wind.frequency, 1.0)) * glm::two_pi<float>() + phase);
                if(wind.flexible) {
                    const auto angles = glm::radians(wind.amplitude * wave);
                    scene.set_vertex_bend(
                        entity, {std::tan(angles.z), -std::tan(angles.x), wind.height, 0});
                    return;
                }
                scene.set_vertex_bend(entity, {});
                auto [it, added] = m_states.try_emplace(
                    entity.get_id(), State{transform.rotation, transform.rotation});
                auto& state = it->second;
                if(transform.rotation != state.output)
                    state.base = transform.rotation;
                state.output = state.base + wind.amplitude * wave;
                entity.edit_transform([&](auto& value) { value.rotation = state.output; });
            });
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
        m_time = 0;
    }
}

#include "scene/transform_interpolation.h"
#include "scene/scene.h"

#include <algorithm>

namespace Comet {
    namespace {
        bool same_transform(const TransformComponent& first, const TransformComponent& second) {
            return first.translation == second.translation && first.rotation == second.rotation
                   && first.scale == second.scale;
        }

        Math::Mat4 interpolated_pose(
            const TransformComponent& previous, const TransformComponent& current, float alpha) {
            const auto position = glm::mix(previous.translation, current.translation, alpha);
            if(previous.rotation == current.rotation)
                return Math::compose_trs(position, current.rotation, Math::Vec3(1));
            const Math::Quat first(glm::radians(Math::wrap_degrees(previous.rotation)));
            const Math::Quat second(glm::radians(Math::wrap_degrees(current.rotation)));
            return Math::translate(Math::Mat4(1), position)
                   * glm::mat4_cast(glm::normalize(glm::slerp(first, second, alpha)));
        }
    }

    void TransformInterpolation::invalidate(Scene& scene) {
        if(m_presented.empty())
            return;
        scene.mark_render_dirty();
        for(const auto& [handle, presented] : m_presented)
            scene.record_render_change(handle);
    }

    void TransformInterpolation::begin_fixed(Scene& scene) {
        m_discontinuities.clear();
        for(auto& [handle, previous] : m_previous)
            previous = scene.m_registry.get<TransformComponent>(handle);
        m_fixed = true;
    }

    void TransformInterpolation::mark_subtree(Scene& scene, const entt::entity handle) {
        m_work.clear();
        m_work.push_back(handle);
        for(std::size_t index = 0; index < m_work.size(); ++index) {
            const auto current = m_work[index];
            auto& presented = m_presented[current];
            if(presented.active)
                continue;
            presented.active = true;
            const auto id = scene.m_registry.get<IdComponent>(current).id;
            const auto children = scene.m_children_by_parent.find(id);
            if(children != scene.m_children_by_parent.end())
                m_work.insert(m_work.end(), children->second.begin(), children->second.end());
        }
    }

    void TransformInterpolation::end_fixed(Scene& scene) {
        m_fixed = false;
        const auto removed = std::erase_if(m_previous, [&](const auto& entry) {
            const auto* current = scene.m_registry.try_get<TransformComponent>(entry.first);
            return !current || same_transform(entry.second, *current);
        });
        // 本步停止移动时，最后一段插值必须收敛到真实姿态。
        if(removed)
            invalidate(scene);
        for(auto& [handle, presented] : m_presented)
            presented.active = false;
        for(const auto& [handle, previous] : m_previous)
            mark_subtree(scene, handle);
        std::erase_if(m_presented, [](const auto& entry) { return !entry.second.active; });
    }

    void TransformInterpolation::record(
        const entt::entity handle, const TransformComponent& previous) {
        if(m_fixed && !m_discontinuities.contains(handle))
            m_previous.try_emplace(handle, previous);
        else
            m_previous.erase(handle);
    }

    void TransformInterpolation::discontinuity(Scene& scene, const entt::entity handle) {
        if(!m_fixed && m_previous.empty())
            return;
        m_work.clear();
        m_work.push_back(handle);
        bool changed = false;
        for(std::size_t index = 0; index < m_work.size(); ++index) {
            const auto current = m_work[index];
            changed |= m_previous.erase(current) != 0;
            if(m_fixed && !m_discontinuities.contains(current))
                m_discontinuities.push(current);
            const auto id = scene.m_registry.get<IdComponent>(current).id;
            const auto children = scene.m_children_by_parent.find(id);
            if(children != scene.m_children_by_parent.end())
                m_work.insert(m_work.end(), children->second.begin(), children->second.end());
        }
        if(changed)
            invalidate(scene);
    }

    void TransformInterpolation::forget(const entt::entity handle) {
        m_previous.erase(handle);
        m_presented.erase(handle);
        m_discontinuities.remove(handle);
    }

    void TransformInterpolation::set_alpha(Scene& scene, const float alpha) {
        if(m_alpha == alpha)
            return;
        m_alpha = alpha;
        invalidate(scene);
    }

    void TransformInterpolation::reset(Scene& scene) {
        if(!m_presented.empty()) {
            scene.mark_render_dirty();
            scene.m_render_full_update = true;
        }
        m_previous.clear();
        m_presented.clear();
        m_discontinuities.clear();
        m_fixed = false;
        m_alpha = 1;
    }

    const WorldTransformComponent& TransformInterpolation::world(
        Scene& scene, const entt::entity handle) {
        const auto& authoritative = scene.m_registry.get<WorldTransformComponent>(handle);
        if(!active())
            return authoritative;
        if(!m_presented.contains(handle)) {
            // 普通更新中新挂接的子节点也继承父级插值；无受影响祖先直接走真实姿态。
            m_work.clear();
            auto current = handle;
            while(!m_presented.contains(current)) {
                m_work.push_back(current);
                const auto parent = scene.m_registry.get<RelationshipComponent>(current).parent;
                if(parent == INVALID_ENTITY_ID)
                    return authoritative;
                current = scene.m_entities_by_id.at(parent);
            }
            for(const auto descendant : m_work)
                m_presented[descendant].active = true;
        }
        m_work.clear();
        auto current = handle;
        for(;;) {
            const auto found = m_presented.find(current);
            if(found == m_presented.end()
                || found->second.source_revision == scene.m_render_revision)
                break;
            m_work.push_back(current);
            const auto parent = scene.m_registry.get<RelationshipComponent>(current).parent;
            if(parent == INVALID_ENTITY_ID)
                break;
            current = scene.m_entities_by_id.at(parent);
        }
        for(auto iterator = m_work.rbegin(); iterator != m_work.rend(); ++iterator)
            update_presented(scene, *iterator);
        return m_presented.at(handle).world;
    }

    void TransformInterpolation::update_presented(Scene& scene, const entt::entity handle) {
        auto& presented = m_presented.at(handle);
        const auto* local = scene.m_registry.try_get<TransformComponent>(handle);
        Math::Mat4 pose_local(1);
        Math::Vec3 scale(1);
        if(local) {
            const auto previous = m_previous.find(handle);
            if(previous == m_previous.end()) {
                pose_local = Math::compose_trs(local->translation, local->rotation, Math::Vec3(1));
                scale = local->scale;
            } else {
                pose_local = interpolated_pose(previous->second, *local, m_alpha);
                scale = glm::mix(previous->second.scale, local->scale, m_alpha);
            }
        }
        auto model = Math::scale(pose_local, scale);
        auto pose = pose_local;
        const auto parent = scene.m_registry.get<RelationshipComponent>(handle).parent;
        if(parent != INVALID_ENTITY_ID) {
            const auto parent_handle = scene.m_entities_by_id.at(parent);
            const auto cached = m_presented.find(parent_handle);
            const auto& parent_world =
                cached != m_presented.end()
                    ? cached->second.world
                    : scene.m_registry.get<WorldTransformComponent>(parent_handle);
            model = parent_world.world_matrix * model;
            pose = parent_world.pose_world_matrix * pose;
        }
        pose[3] = model[3];
        auto& output = presented.world;
        if(output.revision == 0 || output.world_matrix != model || output.pose_world_matrix != pose)
            output.revision = ++scene.m_transform_revision;
        output.world_matrix = model;
        output.pose_world_matrix = pose;
        presented.source_revision = scene.m_render_revision;
    }
}

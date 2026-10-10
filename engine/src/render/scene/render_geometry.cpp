#include "render/scene/render_geometry.h"

#include "render/resource/mesh.h"

#include <algorithm>

namespace Comet {
    namespace {
        void include_bounds(
            std::optional<BoundingBox>& aggregate, const std::optional<BoundingBox>& bounds) {
            if(!bounds)
                return;
            if(aggregate)
                aggregate->include(*bounds);
            else
                aggregate = bounds;
        }
    }

    void RenderGeometry::prepare(const RenderSubmission& submission) {
        clear();
        const auto& items = submission.render_items;
        const auto& changes = submission.item_changes;
        m_items.reserve(items.size());
        const bool same_scene =
            submission.scene_lifetime != 0 && submission.scene_lifetime == m_scene_lifetime;
        const bool tracked = same_scene && changes.revision != 0 && m_bounds.size() == items.size();
        const bool unchanged = tracked && m_revision == changes.revision;
        const bool partial = tracked && !changes.full_update && m_revision == changes.base_revision;
        const auto update_bounds = [&](const std::size_t index) {
            const auto& item = items[index];
            auto& cached = m_bounds[index];
            if(!same_scene || item.entity_id == INVALID_ENTITY_ID || item.transform_revision == 0
                || cached.entity_id != item.entity_id
                || cached.transform_revision != item.transform_revision || !item.mesh_handle
                || item.mesh_revision == 0 || cached.mesh_revision != item.mesh_revision) {
                cached.bounds.reset();
                if(item.mesh)
                    cached.bounds = transform_box(item.mesh->get_local_bounds(), item.model_matrix);
                cached.entity_id = item.entity_id;
                cached.transform_revision = item.transform_revision;
                cached.mesh_revision = item.mesh_revision;
                return true;
            }
            return false;
        };
        bool rebuild = false;
        if(!unchanged && partial) {
            const auto previous_bounds = m_cached_scene_bounds;
            for(const auto index : changes.slots) {
                const auto old = m_bounds[index].bounds;
                if(!update_bounds(index))
                    continue;
                const auto& bounds = m_bounds[index].bounds;
                // 向外扩张可直接合并；旧极值收缩时重新汇总所有界限。
                if(old && previous_bounds) {
                    for(int axis = 0; axis < 3; ++axis) {
                        rebuild |= old->minimum[axis] == previous_bounds->minimum[axis]
                                   && (!bounds || bounds->minimum[axis] > old->minimum[axis]);
                        rebuild |= old->maximum[axis] == previous_bounds->maximum[axis]
                                   && (!bounds || bounds->maximum[axis] < old->maximum[axis]);
                    }
                }
                include_bounds(m_cached_scene_bounds, bounds);
            }
        } else if(!unchanged) {
            const auto previous_size = m_bounds.size();
            // 单个增删确认相邻身份后移动一次后缀；其余重排仍逐槽核对。
            if(same_scene
                && (items.size() == previous_size + 1 || previous_size == items.size() + 1)) {
                std::size_t index = 0;
                while(index < std::min(items.size(), previous_size)
                      && items[index].entity_id == m_bounds[index].entity_id)
                    ++index;
                if(items.size() > previous_size
                    && (index == previous_size
                        || items[index + 1].entity_id == m_bounds[index].entity_id)) {
                    m_bounds.emplace_back();
                    std::move_backward(
                        m_bounds.begin() + index, m_bounds.end() - 1, m_bounds.end());
                    m_bounds[index] = {};
                } else if(previous_size > items.size()
                          && (index == items.size()
                              || items[index].entity_id == m_bounds[index + 1].entity_id)) {
                    std::move(
                        m_bounds.begin() + index + 1, m_bounds.end(), m_bounds.begin() + index);
                    m_bounds.pop_back();
                }
            }
            m_bounds.resize(items.size());
            m_cached_scene_bounds.reset();
            for(std::size_t index = 0; index < items.size(); ++index) {
                update_bounds(index);
                const auto& bounds = m_bounds[index].bounds;
                m_items.push_back({&items[index], bounds});
                include_bounds(m_cached_scene_bounds, bounds);
            }
        }
        // 指针只借用本帧提交；缓存只保存界限值。
        if(unchanged || partial) {
            if(rebuild)
                m_cached_scene_bounds.reset();
            for(std::size_t index = 0; index < items.size(); ++index) {
                m_items.push_back({&items[index], m_bounds[index].bounds});
                if(rebuild)
                    include_bounds(m_cached_scene_bounds, m_bounds[index].bounds);
            }
        }
        m_scene_bounds = m_cached_scene_bounds;
        m_scene_lifetime = submission.scene_lifetime;
        m_revision = changes.revision;
    }

    void RenderGeometry::clear() {
        m_items.clear();
        m_scene_bounds.reset();
    }
}

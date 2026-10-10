#include "render/scene/render_geometry.h"

#include "render/resource/mesh.h"

#include <algorithm>

namespace Comet {
    void RenderGeometry::prepare(const RenderSubmission& submission) {
        clear();
        const auto& items = submission.render_items;
        const bool same_scene =
            submission.scene_lifetime != 0 && submission.scene_lifetime == m_scene_lifetime;
        const auto previous_size = m_bounds.size();
        // 单个增删确认相邻身份后移动一次后缀；其余重排仍逐槽核对。
        if(same_scene && (items.size() == previous_size + 1 || previous_size == items.size() + 1)) {
            std::size_t index = 0;
            while(index < std::min(items.size(), previous_size)
                  && items[index].entity_id == m_bounds[index].entity_id)
                ++index;
            if(items.size() > previous_size
                && (index == previous_size
                    || items[index + 1].entity_id == m_bounds[index].entity_id)) {
                m_bounds.emplace_back();
                std::move_backward(m_bounds.begin() + index, m_bounds.end() - 1, m_bounds.end());
                m_bounds[index] = {};
            } else if(previous_size > items.size()
                      && (index == items.size()
                          || items[index].entity_id == m_bounds[index + 1].entity_id)) {
                std::move(m_bounds.begin() + index + 1, m_bounds.end(), m_bounds.begin() + index);
                m_bounds.pop_back();
            }
        }
        m_items.reserve(items.size());
        m_bounds.resize(items.size());
        for(std::size_t index = 0; index < items.size(); ++index) {
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
            }
            m_items.push_back({&item, cached.bounds});
            if(!cached.bounds)
                continue;
            if(!m_scene_bounds)
                m_scene_bounds = cached.bounds;
            else
                m_scene_bounds->include(*cached.bounds);
        }
        m_scene_lifetime = submission.scene_lifetime;
    }

    void RenderGeometry::clear() {
        m_items.clear();
        m_scene_bounds.reset();
    }
}

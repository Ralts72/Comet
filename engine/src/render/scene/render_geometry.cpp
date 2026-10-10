#include "render/scene/render_geometry.h"

#include "render/resource/mesh.h"

namespace Comet {
    void RenderGeometry::prepare(const RenderSubmission& submission) {
        clear();
        const auto& items = submission.render_items;
        const bool same_scene =
            submission.scene_lifetime != 0 && submission.scene_lifetime == m_scene_lifetime;
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

#include "render/scene/render_geometry.h"

#include "render/resource/mesh.h"

namespace Comet {
    void RenderGeometry::prepare(const std::span<const ResolvedRenderItem> items) {
        clear();
        m_items.reserve(items.size());
        for(const auto& item : items) {
            std::optional<BoundingBox> bounds;
            if(item.mesh)
                bounds = transform_box(item.mesh->get_local_bounds(), item.model_matrix);
            m_items.push_back({&item, bounds});
            if(!bounds)
                continue;
            if(!m_scene_bounds)
                m_scene_bounds = bounds;
            else
                m_scene_bounds->include(*bounds);
        }
    }

    void RenderGeometry::clear() {
        m_items.clear();
        m_scene_bounds.reset();
    }
}

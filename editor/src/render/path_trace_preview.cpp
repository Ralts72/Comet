#include "render/path_trace_preview.h"

namespace CometEditor {
    PathTracePreview::PathTracePreview(const Comet::AssetRegistry& assets,
        Comet::PathTraceSceneBuilder::MeshReader read,
        Comet::PathTraceSceneBuilder::MeshRevision revision)
        : m_resolver(assets), m_read(std::move(read)), m_revision(std::move(revision)) {}

    Comet::Result<std::shared_ptr<const Comet::PathTraceScene>> PathTracePreview::update(
        Comet::Scene& scene) {
        const auto extracted = m_extractor.update(scene);
        const auto world = m_resolver.update(*extracted);
        if(world->render_items.size() != extracted->render_items.size())
            return Comet::Result<std::shared_ptr<const Comet::PathTraceScene>>::failure(
                "场景资源尚未加载，路径追踪等待导入完成");
        return m_builder.update(*world, m_read, m_revision);
    }

    void PathTracePreview::reset() {
        m_builder.reset();
        m_extractor.reset();
        m_resolver.reset();
    }
}

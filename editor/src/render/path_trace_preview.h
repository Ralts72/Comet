#pragma once

#include "render/path_trace_scene.h"
#include "render/scene/scene_extractor.h"
#include "render/scene/scene_resolver.h"

namespace CometEditor {
    class PathTracePreview {
    public:
        PathTracePreview(const Comet::AssetRegistry& assets,
            Comet::PathTraceSceneBuilder::MeshReader read,
            Comet::PathTraceSceneBuilder::MeshRevision revision);
        Comet::Result<std::shared_ptr<const Comet::PathTraceScene>> update(Comet::Scene& scene);
        void reset();

    private:
        Comet::SceneExtractor m_extractor;
        Comet::SceneResolver m_resolver;
        Comet::PathTraceSceneBuilder m_builder;
        Comet::PathTraceSceneBuilder::MeshReader m_read;
        Comet::PathTraceSceneBuilder::MeshRevision m_revision;
    };
}

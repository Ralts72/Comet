#include <gtest/gtest.h>

#include "asset/registry.h"
#include "render/render_scene_resolver.h"

namespace Comet::Tests {
    TEST(RenderSceneResolverTest, EmptySceneProducesEmptySubmission) {
        const AssetRegistry asset_registry;
        RenderSceneResolver resolver(asset_registry);

        const RenderSubmission submission = resolver.resolve(RenderScene{});

        EXPECT_TRUE(submission.render_items.empty());
    }

    TEST(RenderSceneResolverTest, SkipsItemsWithMissingResources) {
        const AssetRegistry asset_registry;
        RenderSceneResolver resolver(asset_registry);
        RenderScene render_scene;
        render_scene.render_items.push_back({
            .entity_id = 7,
            .model_matrix = Math::Mat4(1.0f),
            .mesh_handle = AssetHandle(11),
            .material_handle = AssetHandle(12)
        });

        const RenderSubmission submission = resolver.resolve(render_scene);

        EXPECT_TRUE(submission.render_items.empty());
    }
}

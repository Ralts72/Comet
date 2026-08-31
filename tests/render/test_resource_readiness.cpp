#include "graphics/queue.h"
#include "render/resource/mesh.h"
#include "render/resource/resource_manager.h"
#include "render/resource/texture.h"
#include "render/scene/render_submission.h"
#include "render/scene/scene_renderer.h"

#include <concepts>
#include <gtest/gtest.h>

namespace Comet::Tests {
    namespace {
        template<typename T>
        concept ExposesReadyCompletion = requires(const T& resource) {
            {
                resource.get_ready_completion()
            } -> std::same_as<const GpuCompletionPoint&>;
        };

        template<typename T>
        concept CollectsCompletedUploads = requires(T& manager) {
            manager.collect_completed_uploads();
        };

        template<typename T>
        concept SubmitsResourceWaits = requires(
            T& renderer,
            std::span<const QueueSemaphoreSubmit> waits) {
            renderer.end_frame(waits);
        };

        template<typename T>
        concept BuildsResourceWaits = requires(
            T& renderer,
            const RenderSubmission& submission) {
            {
                renderer.render(submission)
            } -> std::same_as<std::vector<QueueSemaphoreSubmit>>;
        };

        template<typename T>
        concept StoresResourceWaits = requires(T& submission) {
            submission.resource_waits;
        };
    }

    TEST(ResourceReadinessTest, RuntimeGpuResourcesExposeCompletion) {
        EXPECT_TRUE(ExposesReadyCompletion<Mesh>);
        EXPECT_TRUE(ExposesReadyCompletion<Texture>);
        EXPECT_TRUE(CollectsCompletedUploads<ResourceManager>);
        EXPECT_TRUE(SubmitsResourceWaits<SceneRenderer>);
        EXPECT_TRUE(BuildsResourceWaits<SceneRenderer>);
        EXPECT_FALSE(StoresResourceWaits<RenderSubmission>);
    }
}

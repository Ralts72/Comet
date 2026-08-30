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
            std::span<const RenderResourceWait> waits) {
            renderer.end_frame(waits);
        };
    }

    TEST(ResourceReadinessTest, RuntimeGpuResourcesExposeCompletion) {
        EXPECT_TRUE(ExposesReadyCompletion<Mesh>);
        EXPECT_TRUE(ExposesReadyCompletion<Texture>);
        EXPECT_TRUE(CollectsCompletedUploads<ResourceManager>);
        EXPECT_TRUE(SubmitsResourceWaits<SceneRenderer>);
    }

    TEST(ResourceReadinessTest, RenderSubmissionCarriesTypedWaits) {
        RenderSubmission submission;
        submission.resource_waits.push_back({
            .completion = {},
            .stages = Flags<PipelineStage>(PipelineStage::VertexInput)
        });

        ASSERT_EQ(submission.resource_waits.size(), 1U);
        EXPECT_FALSE(submission.resource_waits.front().completion.is_valid());
        EXPECT_EQ(
            submission.resource_waits.front().stages,
            PipelineStage::VertexInput);
    }
}

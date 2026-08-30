#include "graphics/queue.h"
#include "render/resource/mesh.h"
#include "render/resource/resource_manager.h"
#include "render/resource/texture.h"
#include "render/scene/render_submission.h"
#include "render/scene/scene_renderer.h"

#include <concepts>
#include <functional>
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
        concept ConfiguresSwapchainResourceLifecycle = requires(T& renderer) {
            renderer.set_swapchain_resource_callbacks(
                std::function<void()>{},
                std::function<void(const SwapchainCompatibility&)>{});
        };

        template<typename T>
        concept StoresResourceWaits = requires(T& submission) {
            submission.resource_waits;
        };

        template<typename T>
        concept HasRecoverableMeshFactory = requires(
            Device& device,
            UploadManager& upload_manager,
            const MeshData& data) {
            {
                T::try_create(device, upload_manager, data, true)
            } -> std::same_as<GpuResourceResult<std::shared_ptr<Mesh>>>;
        };

        template<typename T>
        concept HasRecoverableTextureFactory = requires(
            Device& device,
            UploadManager& upload_manager,
            const TextureData& data) {
            {
                T::try_create(device, upload_manager, data, true)
            } -> std::same_as<GpuResourceResult<std::shared_ptr<Texture>>>;
        };
    }

    TEST(ResourceReadinessTest, RuntimeGpuResourcesExposeCompletion) {
        EXPECT_TRUE(ExposesReadyCompletion<Mesh>);
        EXPECT_TRUE(ExposesReadyCompletion<Texture>);
        EXPECT_TRUE(CollectsCompletedUploads<ResourceManager>);
        EXPECT_TRUE(SubmitsResourceWaits<SceneRenderer>);
        EXPECT_TRUE(BuildsResourceWaits<SceneRenderer>);
        EXPECT_TRUE(ConfiguresSwapchainResourceLifecycle<SceneRenderer>);
        EXPECT_FALSE(StoresResourceWaits<RenderSubmission>);
        EXPECT_TRUE(HasRecoverableMeshFactory<Mesh>);
        EXPECT_FALSE((std::constructible_from<
            Mesh, Device&, UploadManager&, const MeshData&>));
        EXPECT_TRUE(HasRecoverableTextureFactory<Texture>);
        EXPECT_FALSE((std::constructible_from<
            Texture, Device&, UploadManager&, const TextureData&>));
        EXPECT_FALSE((std::constructible_from<
            Texture, Device&, UploadManager&, int, int, Math::Vec4u>));
    }
}

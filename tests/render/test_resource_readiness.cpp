#include "graphics/synchronization/gpu_completion_point.h"
#include "render/resource/mesh.h"
#include "render/resource/resource_manager.h"
#include "render/resource/texture.h"

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
    }

    TEST(ResourceReadinessTest, RuntimeGpuResourcesExposeCompletion) {
        EXPECT_TRUE(ExposesReadyCompletion<Mesh>);
        EXPECT_TRUE(ExposesReadyCompletion<Texture>);
        EXPECT_TRUE(CollectsCompletedUploads<ResourceManager>);
    }
}

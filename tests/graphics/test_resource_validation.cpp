#include <gtest/gtest.h>

#include "graphics/buffer.h"
#include "graphics/device.h"
#include "graphics/image.h"
#include "render/resource_manager.h"

namespace Comet::Tests {

TEST(ResourceValidationTest, RejectsInvalidBufferArguments) {
    const int data = 42;

    EXPECT_DEATH(
        Buffer::create_cpu_buffer(nullptr, Flags<BufferUsage>(BufferUsage::Uniform), sizeof(data), &data), "");
    EXPECT_DEATH(
        Buffer::create_cpu_buffer(nullptr, Flags<BufferUsage>(BufferUsage::Uniform), 0, &data), "");
    EXPECT_DEATH(
        Buffer::create_gpu_buffer(nullptr, Flags<BufferUsage>(BufferUsage::Vertex), sizeof(data), nullptr), "");
}

TEST(ResourceValidationTest, RejectsInvalidImageArguments) {
    const ImageInfo valid_info = {
        .format = Format::R8G8B8A8_UNORM,
        .extent = Math::Vec3u(1, 1, 1),
        .usage = Flags<ImageUsage>(ImageUsage::Sampled)
    };

    EXPECT_DEATH(Image::create(nullptr, valid_info), "");
    EXPECT_DEATH(Image::wrap(nullptr, VK_NULL_HANDLE, valid_info), "");

    ImageInfo invalid_info = valid_info;
    invalid_info.extent.x = 0;
    EXPECT_DEATH(Image::create(nullptr, invalid_info), "");

    invalid_info = valid_info;
    invalid_info.format = Format::UNDEFINED;
    EXPECT_DEATH(Image::create(nullptr, invalid_info), "");

    invalid_info = valid_info;
    invalid_info.usage = Flags<ImageUsage>();
    EXPECT_DEATH(Image::create(nullptr, invalid_info), "");
}

TEST(ResourceValidationTest, RejectsMissingDeviceDependencies) {
    EXPECT_DEATH({ Device device(nullptr, 1, 1); }, "");
    EXPECT_DEATH({ ResourceManager resource_manager(nullptr); }, "");
}

} // namespace Comet::Tests

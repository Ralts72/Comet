#include <gtest/gtest.h>

#include <type_traits>

#include "graphics/buffer.h"
#include "graphics/device.h"
#include "graphics/image.h"
#include "graphics/sampler.h"

namespace Comet::Tests {

TEST(ResourceValidationTest, RequiredDependenciesRejectNullAtCompileTime) {
    using CpuBufferFactory = decltype(&Buffer::create_cpu_buffer);
    using ImageFactory = decltype(&Image::create);
    using ImageWrapper = decltype(&Image::wrap);

    EXPECT_FALSE((std::is_constructible_v<Device, std::nullptr_t>));
    EXPECT_FALSE((std::is_constructible_v<Sampler, std::nullptr_t, SamplerDesc>));
    EXPECT_FALSE((std::is_invocable_v<CpuBufferFactory, std::nullptr_t,
        Flags<BufferUsage>, size_t, const void*, std::string_view>));
    EXPECT_FALSE((std::is_invocable_v<ImageFactory, std::nullptr_t,
        const ImageInfo&, SampleCount, std::string_view>));
    EXPECT_FALSE((std::is_invocable_v<ImageWrapper, std::nullptr_t,
        vk::Image, const ImageInfo&>));

    EXPECT_TRUE((std::is_invocable_v<CpuBufferFactory, Device&,
        Flags<BufferUsage>, size_t, const void*, std::string_view>));
    EXPECT_TRUE((std::is_invocable_v<ImageFactory, Device&,
        const ImageInfo&, SampleCount, std::string_view>));
    EXPECT_TRUE((std::is_invocable_v<ImageWrapper, Device&,
        vk::Image, const ImageInfo&>));
}

} // namespace Comet::Tests

#include "render/image_denoiser.h"
#include "asset/data/texture_sampling.h"

#include <gtest/gtest.h>
#include <cstring>
#include <random>

namespace CometEditor::Tests {
    using namespace Comet;
    namespace {
        TextureData image(int width, int height, Math::Vec4 value) {
            TextureData data{.width = width,
                .height = height,
                .format = Format::R32G32B32A32_SFLOAT,
                .pixels = std::vector<uint8_t>(std::size_t(width) * height * sizeof(value))};
            for(std::size_t i = 0; i < data.pixels.size(); i += sizeof(value))
                std::memcpy(data.pixels.data() + i, &value, sizeof(value));
            return data;
        }
        void pixel(TextureData& image, std::size_t index, Math::Vec4 value) {
            std::memcpy(image.pixels.data() + index * sizeof(value), &value, sizeof(value));
        }
    }
    TEST(ImageDenoiserTest, RejectsMalformedOrOutOfRangeFeaturesBeforeCreatingADevice) {
        ImageDenoiser denoiser;
        auto color = image(8, 8, {2, 1, .25f, 1});
        auto albedo = image(8, 8, {.5f, .5f, .5f, 1});
        auto normal = image(8, 8, {0, 0, 1, 1});
        auto invalid = normal;
        invalid.pixels.pop_back();
        EXPECT_FALSE(denoiser.filter(color, albedo, invalid));
        invalid = normal;
        invalid.width = 4;
        EXPECT_FALSE(denoiser.filter(color, albedo, invalid));
        pixel(albedo, 2, {1.1f, 0, 0, 1});
        EXPECT_FALSE(denoiser.filter(color, albedo, normal));
        pixel(albedo, 2, {.5f, .5f, .5f, 1});
        pixel(normal, 3, {NAN, 0, 1, 1});
        EXPECT_FALSE(denoiser.filter(color, albedo, normal));
    }
    TEST(ImageDenoiserTest, ReusesDeviceAcrossSizesAndPreservesHdrAlphaAndInputs) {
        ImageDenoiser denoiser;
        for(const auto size : {Math::Vec2u(32, 24), Math::Vec2u(16, 32), Math::Vec2u(32, 24)}) {
            auto color = image(size.x, size.y, {4, .5f, .125f, .7f});
            auto albedo = image(size.x, size.y, {.5f, .5f, .5f, 1});
            auto normal = image(size.x, size.y, {0, 0, 1, 1});
            const auto original = color.pixels;
            auto filtered = denoiser.filter(color, albedo, normal);
            ASSERT_TRUE(filtered) << filtered.error();
            auto decoded = CpuTextureImage::decode(filtered.value(), 1024 * 1024);
            ASSERT_TRUE(decoded);
            Math::Vec3 mean(0);
            for(const auto value : decoded.value().pixels) {
                ASSERT_TRUE(Math::is_finite(value));
                EXPECT_FLOAT_EQ(value.a, .7f);
                mean += Math::Vec3(value);
            }
            mean /= float(decoded.value().pixels.size());
            EXPECT_NEAR(mean.r, 4, .15);
            EXPECT_NEAR(mean.g, .5, .04);
            EXPECT_NEAR(mean.b, .125, .02);
            EXPECT_EQ(color.pixels, original);
        }
    }
    TEST(ImageDenoiserTest, ReducesSeededNoiseWhilePreservingAlbedoEdges) {
        constexpr int size = 64;
        auto color = image(size, size, {0, 0, 0, 1});
        auto albedo = color;
        const auto normal = image(size, size, {0, 0, 1, 1});
        std::mt19937 random(17);
        std::uniform_real_distribution<float> noise(-.6f, .6f);
        std::vector<Math::Vec3> reference(size * size);
        double noisy_error = 0;
        for(int y = 0; y < size; ++y)
            for(int x = 0; x < size; ++x) {
                const auto index = y * size + x;
                const float value = ((x / 8 + y / 8) % 2) ? .75f : .15f;
                reference[index] = Math::Vec3(value * 2);
                const Math::Vec3 noisy(std::max(0.0f, value * 2 + noise(random)));
                pixel(color, index, Math::Vec4(noisy, 1));
                pixel(albedo, index, {value, value, value, 1});
                noisy_error += glm::dot(noisy - reference[index], noisy - reference[index]);
            }
        ImageDenoiser denoiser;
        auto filtered = denoiser.filter(color, albedo, normal);
        ASSERT_TRUE(filtered) << filtered.error();
        auto decoded = CpuTextureImage::decode(filtered.value(), 1024 * 1024);
        ASSERT_TRUE(decoded);
        double filtered_error = 0;
        for(std::size_t i = 0; i < reference.size(); ++i)
            filtered_error += glm::dot(Math::Vec3(decoded.value().pixels[i]) - reference[i],
                Math::Vec3(decoded.value().pixels[i]) - reference[i]);
        RecordProperty("raw_rmse", std::sqrt(noisy_error / (size * size * 3)));
        RecordProperty("denoised_rmse", std::sqrt(filtered_error / (size * size * 3)));
        EXPECT_LT(filtered_error, noisy_error * .5);
    }
}

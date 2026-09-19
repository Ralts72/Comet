#include "graphics/resource/sampler.h"
#include "graphics/device.h"
#include "support/engine_fixture.h"

#include <cmath>
#include <limits>
#include <type_traits>

namespace Comet::Tests {
    static_assert(!std::is_constructible_v<Sampler, Device&, const SamplerDesc&>);
    using SamplerTest = EngineTest;

    TEST_F(SamplerTest, InvalidParametersReturnErrorsBeforeNativeCreation) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        const float limit = device.get_capability().max_sampler_anisotropy;
        for(const float value :
            {0.5f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                std::nextafter(limit, std::numeric_limits<float>::infinity())}) {
            auto result = Sampler::create(device, {.max_anisotropy = value});
            ASSERT_FALSE(result);
            EXPECT_FALSE(result.error().result.has_value());
            EXPECT_FALSE(result.error().message.empty());
        }
        SamplerDesc invalid;
        invalid.mag_filter = static_cast<Filter>(-1);
        EXPECT_FALSE(Sampler::create(device, invalid));
        invalid = {};
        invalid.address_mode_w = static_cast<SamplerAddressMode>(-1);
        EXPECT_FALSE(Sampler::create(device, invalid));
        invalid = {};
        invalid.mipmap_mode = static_cast<SamplerMipmapMode>(-1);
        EXPECT_FALSE(Sampler::create(device, invalid));

        auto valid = Sampler::create(device);
        ASSERT_TRUE(valid) << valid.error();
        EXPECT_TRUE(valid.value()->get());
        EXPECT_EQ(valid.value()->get_description(), SamplerDesc{});
    }

    TEST_F(SamplerTest, FailureDoesNotPublishOrReplaceCachedSampler) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        SamplerManager manager(device);
        auto rejected = manager.create_sampler("custom", {.max_anisotropy = 0.0f});
        ASSERT_FALSE(rejected);
        EXPECT_EQ(manager.get_sampler("custom"), nullptr);
        auto original = manager.create_sampler("custom");
        ASSERT_TRUE(original) << original.error();
        auto reused = manager.create_sampler("custom");
        ASSERT_TRUE(reused) << reused.error();
        EXPECT_EQ(reused.value(), original.value());
        SamplerDesc different;
        different.min_filter = Filter::Nearest;
        auto conflict = manager.create_sampler("custom", different);
        ASSERT_FALSE(conflict);
        EXPECT_FALSE(conflict.error().result.has_value());
        EXPECT_EQ(manager.get_sampler("custom"), original.value());
        EXPECT_EQ(original.value()->get_description(), SamplerDesc{});
        auto still_valid = manager.create_sampler("custom");
        ASSERT_TRUE(still_valid) << still_valid.error();
        EXPECT_EQ(still_valid.value(), original.value());
    }

    TEST_F(SamplerTest, PresetsShareCreationPathAndPreserveTheirConfiguration) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        SamplerManager manager(device);
        auto linear = manager.get_linear_repeat();
        auto nearest = manager.get_nearest_clamp();
        auto shadow = manager.get_shadow_sampler();
        ASSERT_TRUE(linear) << linear.error();
        ASSERT_TRUE(nearest) << nearest.error();
        ASSERT_TRUE(shadow) << shadow.error();
        EXPECT_EQ(linear.value()->get_description().max_anisotropy,
            device.get_capability().max_sampler_anisotropy);
        EXPECT_EQ(linear.value()->get_description().address_mode_u, SamplerAddressMode::Repeat);
        const auto& nearest_desc = nearest.value()->get_description();
        EXPECT_EQ(nearest_desc.mag_filter, Filter::Nearest);
        EXPECT_EQ(nearest_desc.min_filter, Filter::Nearest);
        EXPECT_EQ(nearest_desc.mipmap_mode, SamplerMipmapMode::Nearest);
        EXPECT_EQ(nearest_desc.address_mode_u, SamplerAddressMode::ClampToEdge);
        EXPECT_EQ(nearest_desc.address_mode_v, SamplerAddressMode::ClampToEdge);
        EXPECT_EQ(nearest_desc.address_mode_w, SamplerAddressMode::ClampToEdge);
        EXPECT_EQ(
            shadow.value()->get_description().address_mode_u, SamplerAddressMode::ClampToBorder);
        auto again = manager.get_nearest_clamp();
        ASSERT_TRUE(again) << again.error();
        EXPECT_EQ(again.value(), nearest.value());
        auto conflict = manager.create_sampler("nearest_clamp", {});
        ASSERT_FALSE(conflict);
        EXPECT_EQ(manager.get_sampler("nearest_clamp"), nearest.value());
    }

    TEST_F(SamplerTest, CloseAnisotropyValuesDoNotCollideInPresetCache) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        const float next = std::nextafter(1.0f, 2.0f);
        if(device.get_capability().max_sampler_anisotropy < next)
            GTEST_SKIP() << "Device does not enable anisotropic filtering";
        SamplerManager manager(device);
        ASSERT_EQ(std::to_string(1.0f), std::to_string(next));
        auto first = manager.get_linear_repeat(1.0f);
        auto second = manager.get_linear_repeat(next);
        ASSERT_TRUE(first) << first.error();
        ASSERT_TRUE(second) << second.error();
        EXPECT_NE(first.value(), second.value());
        EXPECT_EQ(first.value()->get_description().max_anisotropy, 1.0f);
        EXPECT_EQ(second.value()->get_description().max_anisotropy, next);
        auto reused = manager.get_linear_repeat(next);
        ASSERT_TRUE(reused) << reused.error();
        EXPECT_EQ(reused.value(), second.value());
    }
}

#include "render/renderer.h"
#include "render/scene/scene_renderer.h"
#include "render/passes/bloom_pass.h"
#include "core/window.h"
#include "config/config.h"
#include "asset/registry.h"

#include <gtest/gtest.h>
#include <chrono>

namespace {
    unsigned creations = 0;
    vk::Result failure = vk::Result::eErrorOutOfDeviceMemory;
}

// 独立测试进程替换工厂；Renderer/SceneRenderer 编译真实实现，生产代码没有注入开关。
namespace Comet {
    Result<std::unique_ptr<BloomPass>, GraphicsError> BloomPass::create(Device&, uint32_t) {
        ++creations;
        return Result<std::unique_ptr<BloomPass>, GraphicsError>::failure(
            {"Injected Bloom allocation failure", failure});
    }
}

TEST(PostProcessRecoveryTest, KeepsRenderingAndBoundsRetriesWithoutChangingAuthoredSettings) {
    using namespace Comet;
    using namespace std::chrono_literals;
    Config config;
    config.window.width = 96;
    config.window.height = 64;
    Window window(config.window);
    AssetRegistry assets;
    auto created = Renderer::create(window, config, assets);
    ASSERT_TRUE(created) << created.error().message;
    auto renderer = std::move(created).value();
    auto& scene = renderer->get_scene_renderer();
    const PostProcessSettings requested{.bloom_enabled = true};
    const auto now = std::chrono::steady_clock::now();
    ASSERT_TRUE(scene.prepare_post_process(requested, now));
    EXPECT_EQ(creations, 1U);
    EXPECT_FALSE(scene.get_post_process_settings().uses_bloom());
    for(const auto elapsed : {500ms, 1000ms, 2000ms, 3000ms, 6000ms, 7000ms, 30000ms})
        ASSERT_TRUE(scene.prepare_post_process(requested, now + elapsed));
    EXPECT_EQ(creations, 4U);
    auto adjusted = requested;
    adjusted.exposure = 2;
    ASSERT_TRUE(scene.prepare_post_process(adjusted, now + 30s));
    EXPECT_EQ(creations, 4U);
    RenderScene submission;
    submission.post_process = requested;
    const auto frame = renderer->prepare_frame();
    ASSERT_TRUE(frame);
    ASSERT_TRUE(frame.value());
    ASSERT_TRUE(renderer->render_frame(submission));
    EXPECT_EQ(creations, 4U);
    EXPECT_EQ(submission.post_process, requested);
    EXPECT_FALSE(scene.get_post_process_settings().uses_bloom());
    renderer->wait_idle();

    ASSERT_TRUE(scene.prepare_post_process({}, now + 31s));
    failure = vk::Result::eErrorDeviceLost;
    const auto lost = scene.prepare_post_process(requested, now + 32s);
    ASSERT_FALSE(lost);
    EXPECT_TRUE(lost.error().is_device_lost());
    EXPECT_EQ(creations, 5U);
}

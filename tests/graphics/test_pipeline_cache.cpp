#include "graphics/pipeline/pipeline_cache.h"
#include "common/file_io.h"
#include "core/engine.h"
#include "core/window.h"
#include "config/config.h"
#include "graphics/context.h"
#include "graphics/device.h"
#include "render/renderer.h"
#include "render/render_context.h"
#include "render/scene/render_scene.h"
#include "support/temporary_directory.h"

#include <algorithm>
#include <gtest/gtest.h>

namespace Comet::Tests {
    class PipelineCacheDataTest: public testing::Test {
    protected:
        vk::PhysicalDeviceProperties properties;
        std::vector<std::byte> data = std::vector<std::byte>(48, std::byte{0x73});
        static void put(std::span<std::byte> bytes, size_t offset, uint32_t value) {
            for(size_t i = 0; i < 4; ++i)
                bytes[offset + i] = std::byte((value >> (i * 8)) & 255);
        }
        void SetUp() override {
            properties.vendorID = 0x11223344;
            properties.deviceID = 0xaabbccdd;
            put(data, 0, 32);
            put(data, 4, 1);
            put(data, 8, properties.vendorID);
            put(data, 12, properties.deviceID);
            for(size_t i = 0; i < VK_UUID_SIZE; ++i) {
                properties.pipelineCacheUUID[i] = static_cast<uint8_t>(i + 1);
                data[16 + i] = std::byte(i + 1);
            }
        }
    };

    TEST_F(PipelineCacheDataTest, RoundTripsBorrowedLittleEndianData) {
        const auto file = PipelineCache::encode(data);
        ASSERT_TRUE(file);
        const auto decoded = PipelineCache::decode(file.value(), properties);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded.value().data(), file.value().data() + 32);
        EXPECT_TRUE(std::ranges::equal(decoded.value(), data));
        EXPECT_EQ(decoded.value()[8], std::byte{0x44});
        EXPECT_EQ(decoded.value()[11], std::byte{0x11});
    }

    TEST_F(PipelineCacheDataTest, RejectsTruncationTrailingBytesAndCorruption) {
        const auto original = PipelineCache::encode(data);
        ASSERT_TRUE(original);
        for(size_t size = 0; size < original.value().size(); ++size)
            EXPECT_FALSE(PipelineCache::decode(std::span(original.value()).first(size), properties))
                << size;
        for(const size_t offset : {0u, 8u, 12u, 16u, 23u, 24u, 40u, 79u}) {
            auto file = original.value();
            file[offset] ^= std::byte{1};
            EXPECT_FALSE(PipelineCache::decode(file, properties)) << offset;
        }
        auto file = original.value();
        file.push_back(std::byte{0});
        EXPECT_FALSE(PipelineCache::decode(file, properties));
    }

    TEST_F(PipelineCacheDataTest, RejectsWrongHeaderDeviceAndOversizedData) {
        for(const size_t offset : {0u, 4u}) {
            auto broken = data;
            put(broken, offset, 64);
            EXPECT_FALSE(PipelineCache::encode(broken));
        }
        for(const size_t offset : {8u, 12u, 16u, 31u}) {
            auto foreign = data;
            foreign[offset] ^= std::byte{1};
            auto file = PipelineCache::encode(foreign);
            ASSERT_TRUE(file);
            EXPECT_FALSE(PipelineCache::decode(file.value(), properties)) << offset;
        }
        const std::vector<std::byte> oversized(PipelineCache::MAX_DATA_SIZE + 33);
        EXPECT_FALSE(PipelineCache::decode(oversized, properties));
        EXPECT_FALSE(PipelineCache::encode(oversized));
    }

    class PipelineCacheRenderingTest: public testing::Test {
    protected:
        TemporaryDirectory directory;
        std::unique_ptr<Engine> engine;

        void start(const std::filesystem::path& cache_directory) {
            engine.reset();
            Config config;
            config.vulkan.enable_validation = true;
            config.vulkan.msaa_samples = SampleCount::Count1;
            config.vulkan.pipeline_cache_directory = cache_directory;
            auto created = Engine::create(config);
            ASSERT_TRUE(created);
            engine = std::move(created).value();
        }
        PipelineCache& cache() {
            return engine->get_renderer().get_render_context().get_device().get_pipeline_cache();
        }
        std::vector<std::byte> read(const std::filesystem::path& path) {
            auto text = read_text_file(path);
            EXPECT_TRUE(text);
            if(!text)
                return {};
            const auto bytes = std::as_bytes(std::span(text.value()));
            return {bytes.begin(), bytes.end()};
        }
        void draw() {
            auto& renderer = engine->get_renderer();
            engine->get_window().poll_events();
            LineDrawList lines;
            ASSERT_TRUE(lines.add_line({-0.5f, 0, -2}, {0.5f, 0, -2}, {1, 0, 0, 1}));
            renderer.submit_lines(lines);
            auto frame = renderer.prepare_frame();
            ASSERT_TRUE(frame);
            ASSERT_EQ(frame.value(), Renderer::FramePreparation::Ready);
            RenderScene scene;
            scene.cameras.push_back(RenderCamera{.primary = true});
            ASSERT_TRUE(renderer.render_frame(scene));
        }
    };

    TEST_F(PipelineCacheRenderingTest, PersistsAtShutdownAndRestoresRealPipelines) {
        start(directory.path());
        ASSERT_TRUE(engine);
        EXPECT_EQ(cache().get_load_status(), PipelineCache::LoadStatus::Missing);
        const auto path = cache().get_path();
        EXPECT_FALSE(std::filesystem::exists(path));
        draw();
        engine.reset();
        ASSERT_TRUE(std::filesystem::is_regular_file(path));
        start(directory.path());
        ASSERT_TRUE(engine);
        EXPECT_EQ(cache().get_load_status(), PipelineCache::LoadStatus::Restored);
        draw();
        EXPECT_TRUE(cache().save());
    }

    TEST_F(PipelineCacheRenderingTest, RejectsBadFilesWithoutReplacingThemUntilSave) {
        start(directory.path());
        ASSERT_TRUE(engine);
        const auto path = cache().get_path();
        const auto properties = engine->get_renderer().get_render_context().get_context()
                                    .get_physical_device().getProperties();
        engine.reset();
        const auto valid = read(path);
        auto original = PipelineCache::decode(valid, properties);
        ASSERT_TRUE(original);
        std::vector<std::byte> foreign(original.value().begin(), original.value().end());
        foreign[8] ^= std::byte{1};
        auto encoded = PipelineCache::encode(foreign);
        ASSERT_TRUE(encoded);
        auto corrupted = valid;
        corrupted.back() ^= std::byte{1};
        for(const auto& input : {std::vector<std::byte>(16), corrupted, encoded.value()}) {
            ASSERT_TRUE(write_binary_file_atomic(path, input));
            start(directory.path());
            ASSERT_TRUE(engine);
            EXPECT_EQ(cache().get_load_status(), PipelineCache::LoadStatus::Rejected);
            EXPECT_EQ(read(path), input);
            draw();
            engine.reset();
            const auto repaired = read(path);
            EXPECT_TRUE(PipelineCache::decode(repaired, properties));
        }
    }

    TEST_F(PipelineCacheRenderingTest,
        SaveFailurePreservesDestinationAndDisabledCacheWritesNothing) {
        start(directory.path());
        ASSERT_TRUE(engine);
        const auto path = cache().get_path();
        std::filesystem::create_directories(path);
        ASSERT_TRUE(write_text_file_atomic(path / "sentinel", "keep"));
        EXPECT_FALSE(cache().save());
        engine.reset();
        auto sentinel = read_text_file(path / "sentinel");
        ASSERT_TRUE(sentinel);
        EXPECT_EQ(sentinel.value(), "keep");
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory.path()),
                      std::filesystem::directory_iterator{}), 1);
        start({});
        ASSERT_TRUE(engine);
        EXPECT_EQ(cache().get_load_status(), PipelineCache::LoadStatus::Disabled);
        EXPECT_TRUE(cache().get_path().empty());
        EXPECT_TRUE(cache().save());
        draw();
    }
}

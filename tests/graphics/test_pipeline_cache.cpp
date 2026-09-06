#include "graphics/pipeline/pipeline_cache.h"
#include "core/engine.h"
#include "common/file_io.h"
#include "diagnostics/logger.h"

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <algorithm>
#include <chrono>
#include <sstream>

namespace Comet::Tests {
    class PipelineCacheDataTest: public testing::Test {
    protected:
        vk::PhysicalDeviceProperties properties;
        std::vector<std::byte> data = std::vector<std::byte>(48, std::byte{0x73});
        static void put(std::span<std::byte> bytes, size_t offset, uint32_t value) {
            for(size_t index = 0; index < 4; ++index)
                bytes[offset + index] = std::byte((value >> (index * 8)) & 255);
        }
        void SetUp() override {
            properties.vendorID = 0x11223344;
            properties.deviceID = 0xaabbccdd;
            put(data, 0, 32);
            put(data, 4, 1);
            put(data, 8, properties.vendorID);
            put(data, 12, properties.deviceID);
            for(size_t index = 0; index < VK_UUID_SIZE; ++index) {
                properties.pipelineCacheUUID[index] = static_cast<uint8_t>(index + 1);
                data[16 + index] = std::byte(index + 1);
            }
        }
    };

    TEST_F(PipelineCacheDataTest,
        RoundTripsLittleEndianEnvelopeWithoutBorrowingTemporaryStorage) {
        const auto file = PipelineCache::encode(data);
        const auto decoded = PipelineCache::decode(file, properties);
        EXPECT_EQ(decoded.data(), file.data() + 32);
        EXPECT_TRUE(std::ranges::equal(decoded, data));
        EXPECT_EQ(file.size(), data.size() + 32);
        EXPECT_EQ(decoded[8], std::byte{0x44});
        EXPECT_EQ(decoded[11], std::byte{0x11});
    }

    TEST_F(PipelineCacheDataTest,
        RejectsTruncationTrailingBytesAndDamagedEnvelopeOrPayload) {
        const auto original = PipelineCache::encode(data);
        for(size_t size = 0; size < original.size(); ++size) {
            EXPECT_THROW(static_cast<void>(PipelineCache::decode(
                             std::span(original).first(size), properties)),
                std::invalid_argument)
                << size;
        }
        for(const size_t offset : {0u, 8u, 12u, 16u, 23u, 24u, 40u, 79u}) {
            auto file = original;
            file[offset] ^= std::byte{1};
            EXPECT_THROW(static_cast<void>(PipelineCache::decode(file, properties)),
                std::invalid_argument)
                << offset;
        }
        auto file = original;
        file.push_back(std::byte{0});
        EXPECT_THROW(static_cast<void>(PipelineCache::decode(file, properties)),
            std::invalid_argument);
    }

    TEST_F(PipelineCacheDataTest, RejectsHeaderVersionSizeAndEachDeviceIdentityField) {
        for(const size_t offset : {0u, 4u}) {
            auto broken = data;
            put(broken, offset, 64);
            EXPECT_THROW(
                static_cast<void>(PipelineCache::encode(broken)), std::invalid_argument);
        }
        for(const size_t offset : {8u, 12u, 16u, 31u}) {
            auto other_device = data;
            other_device[offset] ^= std::byte{1};
            const auto file = PipelineCache::encode(other_device);
            EXPECT_THROW(static_cast<void>(PipelineCache::decode(file, properties)),
                std::invalid_argument)
                << offset;
        }
        std::vector<std::byte> oversized(PipelineCache::MAX_DATA_SIZE + 33);
        EXPECT_THROW(static_cast<void>(PipelineCache::decode(oversized, properties)),
            std::invalid_argument);
        EXPECT_THROW(
            static_cast<void>(PipelineCache::encode(oversized)), std::invalid_argument);
    }

    class PipelineCacheRenderingTest: public testing::Test {
    protected:
        std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / ("comet_pipeline_cache_" + std::to_string(AssetHandle::generate().value()));
        std::unique_ptr<Engine> engine;
        std::ostringstream messages;
        std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink;
        void SetUp() override {
            sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(messages);
            Logger::add_custom_sink(sink);
        }
        void TearDown() override {
            engine.reset();
            if(auto logger = Logger::get_console_logger())
                std::erase(logger->sinks(), sink);
            EXPECT_EQ(messages.str().find("VUID-"), std::string::npos) << messages.str();
            EXPECT_EQ(messages.str().find("Validation Error"), std::string::npos)
                << messages.str();
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
        double start(const std::filesystem::path& directory) {
            engine.reset();
            Config config;
            config.window.width = 160;
            config.window.height = 120;
            config.vulkan.enable_validation = true;
            config.vulkan.msaa_samples = SampleCount::Count1;
            config.vulkan.pipeline_cache_directory = directory;
            const auto began = std::chrono::steady_clock::now();
            engine = std::make_unique<Engine>(config);
            return std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - began)
                .count();
        }
        PipelineCache& cache() {
            return engine->get_renderer()
                .get_render_context()
                .get_device()
                .get_pipeline_cache();
        }
        static std::vector<std::byte> read(const std::filesystem::path& path) {
            const auto text = read_text_file(path);
            const auto bytes = std::as_bytes(std::span(text));
            return {bytes.begin(), bytes.end()};
        }
        void draw() {
            auto& renderer = engine->get_renderer();
            engine->get_window().poll_events();
            LineDrawList lines;
            ASSERT_TRUE(lines.add_line({-0.5f, 0, -2}, {0.5f, 0, -2}, {1, 0, 0, 1}));
            renderer.submit_lines(lines);
            ASSERT_TRUE(renderer.prepare_frame());
            RenderScene scene;
            scene.cameras.push_back(RenderCamera{.primary = true});
            renderer.render_frame(scene);
        }
    };

    TEST_F(PipelineCacheRenderingTest,
        PersistsAtShutdownRestoresOnNewDeviceAndCreatesRealPipelines) {
        const auto cold_ms = start(root / "vulkan");
        EXPECT_EQ(cache().get_load_status(), PipelineCache::LoadStatus::Missing);
        const auto path = cache().get_path();
        EXPECT_FALSE(std::filesystem::exists(path));
        draw();
        engine.reset();
        ASSERT_TRUE(std::filesystem::is_regular_file(path));
        const auto file = read(path);
        const auto warm_ms = start(root / "vulkan");
        EXPECT_EQ(cache().get_load_status(), PipelineCache::LoadStatus::Restored);
        const auto properties = engine->get_renderer()
                                    .get_render_context()
                                    .get_context()
                                    .get_physical_device()
                                    .getProperties();
        EXPECT_GE(PipelineCache::decode(file, properties).size(), 32u);
        draw();
        ASSERT_TRUE(cache().save());
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(path.parent_path()),
                      std::filesystem::directory_iterator{}),
            1);
        RecordProperty("cold_engine_ms", std::to_string(cold_ms));
        RecordProperty("warm_engine_ms", std::to_string(warm_ms));
        RecordProperty("cache_file_bytes", std::to_string(file.size()));
    }

    TEST_F(PipelineCacheRenderingTest,
        DamagedOrForeignCacheFallsBackAndIsReplacedOnlyAtSave) {
        start(root / "vulkan");
        const auto path = cache().get_path();
        const auto properties = engine->get_renderer()
                                    .get_render_context()
                                    .get_context()
                                    .get_physical_device()
                                    .getProperties();
        engine.reset();
        const auto valid = read(path);
        auto truncated = valid;
        truncated.resize(16);
        auto damaged = valid;
        damaged.back() ^= std::byte{1};
        const auto original_data = PipelineCache::decode(valid, properties);
        std::vector<std::byte> foreign(original_data.begin(), original_data.end());
        foreign[8] ^= std::byte{1};
        for(const auto& input : {truncated, damaged, PipelineCache::encode(foreign)}) {
            write_binary_file_atomic(path, input);
            start(root / "vulkan");
            EXPECT_EQ(cache().get_load_status(), PipelineCache::LoadStatus::Rejected);
            EXPECT_EQ(read(path), input);
            draw();
            engine.reset();
            EXPECT_NO_THROW(
                static_cast<void>(PipelineCache::decode(read(path), properties)));
        }
    }

    TEST_F(PipelineCacheRenderingTest,
        SaveFailureKeepsExistingDestinationAndDoesNotStopShutdown) {
        start(root / "vulkan");
        const auto path = cache().get_path();
        std::filesystem::create_directories(path);
        write_text_file_atomic(path / "sentinel", "keep me");
        EXPECT_FALSE(cache().save());
        engine.reset();
        EXPECT_EQ(read_text_file(path / "sentinel"), "keep me");
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(path.parent_path()),
                      std::filesystem::directory_iterator{}),
            1);
        start({});
        EXPECT_EQ(cache().get_load_status(), PipelineCache::LoadStatus::Disabled);
        EXPECT_TRUE(cache().get_path().empty());
        EXPECT_FALSE(cache().save());
        draw();
    }
}

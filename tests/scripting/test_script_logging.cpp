#include "scripting/script.h"
#include "common/file_io.h"
#include "diagnostics/logger.h"
#include "scene/scene.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <spdlog/sinks/callback_sink.h>
#include <algorithm>
#include <array>

namespace Comet::Tests {
    class ScriptLoggingTest: public testing::Test {
    protected:
        std::vector<std::pair<spdlog::level::level_enum, std::string>> messages;
        std::shared_ptr<spdlog::logger> logger;
        std::shared_ptr<spdlog::sinks::sink> sink;
        spdlog::level::level_enum previous_level{};
        bool owns_logger = false;

        void SetUp() override {
            owns_logger = !Logger::get_console_logger();
            Config::Log config;
            config.enable_file_logging = false;
            Logger::init(config);
            logger = Logger::get_console_logger();
            previous_level = logger->level();
            logger->set_level(spdlog::level::info);
            sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
                [&](const spdlog::details::log_msg& message) {
                    messages.emplace_back(
                        message.level, std::string(message.payload.data(), message.payload.size()));
                });
            Logger::add_custom_sink(sink);
        }

        void TearDown() override {
            std::erase(logger->sinks(), sink);
            logger->set_level(previous_level);
            if(owns_logger)
                Logger::shutdown();
        }
    };

    TEST_F(ScriptLoggingTest, CallbacksAndModulesLogWithoutPreparationSideEffects) {
        TemporaryDirectory directory;
        ASSERT_TRUE(write_text_file_atomic(directory.path() / "helper.module.lua",
            "return {report = function() comet.log('count {} 中文') end}"));
        ASSERT_TRUE(write_text_file_atomic(directory.path() / "actor.lua", R"(
            local helper = require('helper')
            return {
                on_start = function() comet.log('start') end,
                update = function() helper.report() end,
                events = {message = 'message'},
                message = function(self, value) comet.log(value) end,
                on_stop = function() comet.log('stop') end,
            }
        )"));
        const std::array<std::filesystem::path, 1> roots{"actor.lua"};
        const auto scripts = Script::load_group(directory.path(), roots);
        ASSERT_TRUE(scripts) << scripts.error().message;
        const auto instance = scripts.value()[0]->instantiate();
        ASSERT_TRUE(instance);
        EXPECT_TRUE(messages.empty());
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Start, {}, {}));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Update, {}, {}));
        const ParameterValue value = std::string("event");
        ASSERT_TRUE(instance.value()->invoke(
            Script::Phase::Event, {}, {}, {.event_handler = "message", .event_value = &value}));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Stop, {}, {}));
        ASSERT_EQ(messages.size(), 4u);
        for(const auto& [level, text] : messages) {
            EXPECT_EQ(level, spdlog::level::info);
            EXPECT_TRUE(text.starts_with("[Lua] "));
        }
        EXPECT_NE(messages[0].second.find("actor.lua:"), std::string::npos);
        EXPECT_TRUE(messages[0].second.ends_with(": start"));
        EXPECT_NE(messages[1].second.find("helper.module.lua:1:"), std::string::npos);
        EXPECT_TRUE(messages[1].second.ends_with(": count {} 中文"));
        EXPECT_TRUE(messages[2].second.ends_with(": event"));
        EXPECT_TRUE(messages[3].second.ends_with(": stop"));

        logger->set_level(spdlog::level::warn);
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Update, {}, {}));
        EXPECT_EQ(messages.size(), 4u);
    }

    TEST_F(ScriptLoggingTest, PreparationAndInvalidArgumentsProduceNoLogOutput) {
        const auto top_level = Script::create("comet.log('preparing'); return {}");
        ASSERT_FALSE(top_level);
        EXPECT_NE(top_level.error().message.find("callbacks"), std::string::npos);
        for(const auto* arguments : {"", "nil", "42", "{}", "'one', 'two'"}) {
            SCOPED_TRACE(arguments);
            const auto script = Script::create(
                std::string("return {update = function() comet.log(") + arguments + ") end}");
            ASSERT_TRUE(script);
            const auto instance = script.value()->instantiate();
            ASSERT_TRUE(instance);
            const auto invoked = instance.value()->invoke(Script::Phase::Update, {}, {});
            ASSERT_FALSE(invoked);
            EXPECT_NE(invoked.error().message.find("exactly one string"), std::string::npos);
        }
        EXPECT_TRUE(messages.empty());
    }

    TEST_F(ScriptLoggingTest, OutputLimitsDoNotAbortCallbacksAndResetForEachInvocation) {
        const auto script = Script::create(R"(return {
            update = function()
                comet.log(string.rep('x', 4097))
                for i = 1, 20 do comet.log('line ' .. i) end
                comet.translate(1, 0, 0)
            end
        })",
            "budget.lua");
        ASSERT_TRUE(script);
        const auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        Scene scene;
        const auto entity = scene.create_entity();
        for(int call = 1; call <= 2; ++call) {
            ASSERT_TRUE(
                instance.value()->invoke(Script::Phase::Update, entity, {}, {.scene = &scene}));
            EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().translation.x, call);
            EXPECT_EQ(std::ranges::count(messages, spdlog::level::info,
                          [](const auto& message) { return message.first; }),
                16 * call);
            EXPECT_EQ(std::ranges::count(messages, spdlog::level::warn,
                          [](const auto& message) { return message.first; }),
                call);
        }
        EXPECT_TRUE(std::ranges::all_of(
            messages, [](const auto& message) { return message.second.size() < 256; }));
    }
}

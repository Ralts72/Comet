#include "scripting/script.h"
#include "scene/entity.h"
#include <gtest/gtest.h>

namespace Comet::Tests {
    TEST(ScriptSourceTest, ValidatesOverridesWithoutChangingDefaults) {
        auto script = Script::create("return {properties = {speed = 1, enabled = true}}");
        ASSERT_TRUE(script);
        EXPECT_TRUE(script.value()->validate_overrides({}));
        const ParameterMap overrides{{"speed", 2.0f}};
        EXPECT_TRUE(script.value()->validate_overrides(overrides));
        auto resolved = script.value()->resolve_parameters(overrides);
        ASSERT_TRUE(resolved);
        EXPECT_EQ(std::get<float>(resolved.value().at("speed")), 2);
        EXPECT_TRUE(std::get<bool>(resolved.value().at("enabled")));
        EXPECT_EQ(std::get<float>(script.value()->defaults().at("speed")), 1);
        EXPECT_FALSE(script.value()->validate_overrides({{"missing", 2.0f}}));
        EXPECT_FALSE(script.value()->resolve_parameters({{"speed", false}}));
    }

    TEST(ScriptInvocationTest, ParameterChangesAndFailureInvalidateOnlyTheConfigurationCache) {
        auto script = Script::create(R"(return {
            properties = {speed = 1},
            update = function(self, dt)
                assert(self.parameters.speed == dt)
                if dt == 13 then error('update failed') end
            end,
            on_stop = function(self) assert(self.parameters.speed == 3) end,
        })");
        ASSERT_TRUE(script);
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        // VM 加载后独立持有 Lua 代码，不借用 Script 的源码存储。
        script.value().reset();
        for(const float speed : {2.0f, 2.0f, 3.0f})
            ASSERT_TRUE(instance.value()->invoke(
                Script::Phase::Update, {}, {{"speed", speed}}, {.delta_time = speed}));
        auto failed = instance.value()->invoke(
            Script::Phase::Update, {}, {{"speed", 13.0f}}, {.delta_time = 13});
        ASSERT_FALSE(failed);
        EXPECT_NE(failed.error().message.find("update failed"), std::string::npos);
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Stop, {}, {{"speed", 3.0f}}));
    }

    TEST(ScriptSourceTest, InvalidCodeSchemaAndUnsafeLibrariesFailWithoutProcessTermination) {
        EXPECT_FALSE(Script::create("return {"));
        EXPECT_FALSE(Script::create("return 42"));
        EXPECT_FALSE(Script::create("return {update = true}"));
        EXPECT_FALSE(Script::create("return {properties = {value = function() end}}"));
        EXPECT_FALSE(Script::create("return {properties = {value = math.huge}}"));
        EXPECT_FALSE(Script::create("while true do end"));
        EXPECT_FALSE(
            Script::create("return {properties = {value = string.rep('x', 32*1024*1024)}}"));
        EXPECT_TRUE(Script::create(
            "assert(io == nil and os == nil and package == nil and debug == nil and load == nil and pcall == nil); return {}"));
        EXPECT_FALSE(Script::create("comet.rotate(0, 1, 0); return {}"));
    }

    TEST(ScriptInvocationTest, CachedParametersAreReadOnlyAndStopErrorsAreObservable) {
        const auto script = Script::create(R"(return {
            properties = {speed = 1, direction = {1, 2, 3}},
            on_start = function(self) self.config = self.parameters; self.steps = 0 end,
            update = function(self)
                assert(self.parameters == self.config)
                assert(#self.parameters.direction == 3)
                local count = 0
                for k, v in pairs(self.parameters) do count = count + 1 end
                assert(count == 2)
                self.steps = self.steps + 1
            end,
            on_stop = function(self)
                assert(self.steps == 2)
                self.parameters.direction[1] = 0
            end
        })");
        ASSERT_TRUE(script);
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        const auto& parameters = script.value()->defaults();
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Start, {}, parameters));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Update, {}, parameters));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Update, {}, parameters));
        auto stopped = instance.value()->invoke(Script::Phase::Stop, {}, parameters);
        ASSERT_FALSE(stopped);
        EXPECT_NE(stopped.error().message.find("read-only"), std::string::npos);
    }
}

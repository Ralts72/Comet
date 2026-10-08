#include "scripting/script.h"
#include "physics/physics_service.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "input/input_actions.h"
#include "input/input_state.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"
#include <gtest/gtest.h>
#include <array>
#include <limits>
#include <utility>

namespace Comet::Tests {
    TEST(ScriptTextTest, Utf8BomWorksForMemoryAndStandaloneSourcesWithoutRewritingTheFile) {
        const std::string source =
            "\xef\xbb\xbf"
            "local script = {}\r\n"
            "script.properties = {speed = 7}\r\n"
            "function script:update() assert(self.parameters.speed == 7) end\r\n"
            "return script\r\n";
        const auto memory = Script::create(source, "memory.lua");
        ASSERT_TRUE(memory) << memory.error().message;
        TemporaryDirectory directory;
        const auto path = directory.path() / "actor.lua";
        ASSERT_TRUE(write_text_file_atomic(path, source));
        const auto file = Script::load(path);
        ASSERT_TRUE(file) << file.error().message;
        for(const auto& script : {memory.value(), file.value()}) {
            const auto instance = script->instantiate();
            ASSERT_TRUE(instance) << instance.error().message;
            const auto parameters = script->resolve_parameters({});
            ASSERT_TRUE(parameters);
            EXPECT_TRUE(instance.value()->invoke(Script::Phase::Update, {}, parameters.value()));
        }
        EXPECT_EQ(read_text_file(path).value(), source);
        for(const auto* prefix : {"\xef", "\xef\xbb", "\xff\xfe", "\xef\xbb\xbf\xef\xbb\xbf"})
            EXPECT_FALSE(Script::create(std::string(prefix) + "return {}"));
    }

    TEST(ScriptModulePathTest, NamesAndProjectRelativePathsUseOneExactAsciiMapping) {
        for(const auto& [name, path] : {std::pair{"score", "score.module.lua"},
                std::pair{"scripts.demo_score", "scripts/demo_score.module.lua"},
                std::pair{"Scripts.Score_2", "Scripts/Score_2.module.lua"},
                std::pair{"_shared.rules.v2", "_shared/rules/v2.module.lua"}}) {
            SCOPED_TRACE(name);
            const auto mapped = Script::module_path(name);
            ASSERT_TRUE(mapped) << mapped.error();
            EXPECT_EQ(mapped.value(), path);
            const auto reversed = Script::module_name(mapped.value());
            ASSERT_TRUE(reversed) << reversed.error();
            EXPECT_EQ(reversed.value(), name);
        }
        const auto boundary = std::string(63, 'a') + "." + std::string(63, 'b') + "."
                              + std::string(63, 'c') + "." + std::string(64, 'd');
        ASSERT_EQ(boundary.size(), 256u);
        const auto mapped = Script::module_path(boundary);
        ASSERT_TRUE(mapped) << mapped.error();
        const auto reversed = Script::module_name(mapped.value());
        ASSERT_TRUE(reversed) << reversed.error();
        EXPECT_EQ(reversed.value(), boundary);
        EXPECT_FALSE(Script::module_path(boundary + "x"));
        EXPECT_FALSE(Script::module_name(std::string(257, 'a') + ".module.lua"));
    }

    TEST(ScriptModulePathTest, RejectsUnsafeNamesAndPathsThatWouldAliasAnotherModule) {
        for(const auto& name :
            {std::string{}, std::string("."), std::string(".one"), std::string("one."),
                std::string("one..two"), std::string("../outside"), std::string("one/two"),
                std::string("one\\two"), std::string("1wrong"), std::string("one.2wrong"),
                std::string("-bad"), std::string("one.two-module"), std::string("one:two"),
                std::string(" one"), std::string("脚本.score"), std::string("one\0two", 7)}) {
            SCOPED_TRACE(name);
            EXPECT_FALSE(Script::module_path(name));
        }
        for(const auto& path :
            {std::string{}, std::string("/score.module.lua"), std::string("../score.module.lua"),
                std::string("scripts/../score.module.lua"), std::string("./score.module.lua"),
                std::string("score.lua"), std::string("score.Module.lua"),
                std::string("score.module.LUA"), std::string("foo.bar.module.lua"),
                std::string("a.b/score.module.lua"), std::string("scripts\\score.module.lua"),
                std::string("1bad.module.lua"), std::string("scripts/2bad.module.lua"),
                std::string("score.module.lua/"), std::string("脚本/score.module.lua"),
                std::string("C:/score.module.lua"), std::string("score") + '\0' + ".module.lua"}) {
            SCOPED_TRACE(path);
            EXPECT_FALSE(Script::module_name(path));
        }
    }

    class ScriptModulesTest: public ::testing::Test {
    protected:
        TemporaryDirectory directory;

        void write(const std::filesystem::path& path, const std::string_view source) {
            const auto written = write_text_file_atomic(directory.path() / path, source);
            ASSERT_TRUE(written) << written.error();
        }

        auto load(const std::filesystem::path& path = "actor.lua") {
            const std::array paths{path};
            return Script::load_group(directory.path(), paths);
        }
    };

    TEST_F(ScriptModulesTest, ModuleBomPreservesLineNumbersAndExactSourceFreshness) {
        const std::string module = "local module = {}\r\n"
                                   "function module.fail()\r\n"
                                   "    error('bom line')\r\n"
                                   "end\r\nreturn module\r\n";
        write("value.module.lua", "\xef\xbb\xbf" + module);
        write("actor.lua", "local value = require('value')\r\n"
                           "return {update = function() value.fail() end}\r\n");
        const auto group = load();
        ASSERT_TRUE(group) << group.error().message;
        const auto script = group.value().front();
        EXPECT_TRUE(script->inputs_are_current());
        const auto instance = script->instantiate();
        ASSERT_TRUE(instance);
        const auto failed = instance.value()->invoke(Script::Phase::Update, {}, {});
        ASSERT_FALSE(failed);
        EXPECT_NE(failed.error().message.find("value.module.lua:3:"), std::string::npos);
        EXPECT_NE(failed.error().message.find("actor.lua:2:"), std::string::npos);

        write("value.module.lua", module);
        EXPECT_FALSE(script->inputs_are_current());
        const auto changed = load();
        ASSERT_TRUE(changed);
        EXPECT_FALSE(script->has_same_sources(*changed.value().front()));
        const auto retained = script->instantiate();
        ASSERT_TRUE(retained);
        const auto old_failure = retained.value()->invoke(Script::Phase::Update, {}, {});
        ASSERT_FALSE(old_failure);
        EXPECT_NE(old_failure.error().message.find("value.module.lua:3:"), std::string::npos);
    }

    TEST_F(ScriptModulesTest, NestedModulesAreCachedPerInstanceAndKeepTheirSourceSnapshot) {
        write("scripts/value.module.lua", "return {base = 7}");
        write("scripts/counter.module.lua", R"(
            local value = require('scripts.value')
            module_loads = (module_loads or 0) + 1
            assert(module_loads == 1)
            local count = 0
            return {base = value.base, next = function() count = count + 1; return count end}
        )");
        write("actor.lua", R"(
            local counter = require('scripts.counter')
            assert(counter == require('scripts.counter'))
            assert(counter.base == require('scripts.value').base)
            assert(counter.next() == 1)
            return {properties = {base = counter.base}, update = function(self)
                self.count = (self.count or 1) + 1
                assert(counter.next() == self.count)
                assert(counter == require('scripts.counter'))
            end}
        )");
        auto group = load();
        ASSERT_TRUE(group) << group.error().message;
        ASSERT_EQ(group.value().size(), 1u);
        auto script = group.value().front();
        EXPECT_EQ(script->source_path(), "actor.lua");
        EXPECT_EQ(
            script->dependencies(), (std::vector<std::filesystem::path>{
                                        "scripts/counter.module.lua", "scripts/value.module.lua"}));
        EXPECT_EQ(std::get<float>(script->properties().at("base").default_value), 7.0f);
        EXPECT_TRUE(script->inputs_are_current());
        write("scripts/value.module.lua", "return {base = 9}");
        EXPECT_FALSE(script->inputs_are_current());
        auto first = script->instantiate();
        auto second = script->instantiate();
        ASSERT_TRUE(first) << first.error().message;
        ASSERT_TRUE(second) << second.error().message;
        group.value().clear();
        script.reset();
        for(int step = 0; step < 3; ++step) {
            EXPECT_TRUE(first.value()->invoke(Script::Phase::Update, {}, {}));
            EXPECT_TRUE(second.value()->invoke(Script::Phase::Update, {}, {}));
        }
    }

    TEST_F(ScriptModulesTest, MethodsKeepDefinitionAndModuleTablesIsolatedPerInstance) {
        write("counter.module.lua", R"(
            local counter = {value = 0}
            function counter:add(amount)
                self.value = self.value + amount
                return self.value
            end
            return counter
        )");
        write("actor.lua", R"(
            local group = {
                properties = {expected = 0},
                history = {total = 0}, counter = require('counter'),
            }
            function group:advance(amount)
                self.history.total = self.history.total + amount
                return self.counter:add(amount), self.history.total
            end
            function group:update(dt)
                assert(rawget(self, 'history') == nil)
                assert(self.counter == require('counter'))
                local module_total, own_total = self:advance(dt)
                assert(module_total == self.parameters.expected)
                assert(own_total == self.parameters.expected)
            end
            return group
        )");
        const auto group = load();
        ASSERT_TRUE(group) << group.error().message;
        const auto first = group.value().front()->instantiate();
        const auto second = group.value().front()->instantiate();
        ASSERT_TRUE(first) << first.error().message;
        ASSERT_TRUE(second) << second.error().message;

        ASSERT_TRUE(first.value()->invoke(
            Script::Phase::Update, {}, {{"expected", 2.0f}}, {.delta_time = 2}));
        ASSERT_TRUE(first.value()->invoke(
            Script::Phase::Update, {}, {{"expected", 5.0f}}, {.delta_time = 3}));
        ASSERT_TRUE(second.value()->invoke(
            Script::Phase::Update, {}, {{"expected", 7.0f}}, {.delta_time = 7}));
        ASSERT_TRUE(first.value()->invoke(
            Script::Phase::Update, {}, {{"expected", 6.0f}}, {.delta_time = 1}));
    }

    TEST_F(ScriptModulesTest, InvocationErrorsTraceEntryHelperAndModuleWithoutAccumulatingStack) {
        write("scripts/fault.module.lua", R"(local module = {}
function module.fail()
    error('module exploded')
end
return module
)");
        write("actor.lua", R"(local fault = require('scripts.fault')
local group = {}
function group:helper()
    fault.fail()
end
function group:update()
    self:helper()
end
function group:on_stop()
    assert(debug == nil and getmetatable == nil and pcall == nil)
end
return group
)");
        const auto group = load();
        ASSERT_TRUE(group) << group.error().message;
        const auto instance = group.value().front()->instantiate();
        ASSERT_TRUE(instance) << instance.error().message;
        const auto failed = instance.value()->invoke(Script::Phase::Update, {}, {});
        ASSERT_FALSE(failed);
        const auto& message = failed.error().message;
        EXPECT_TRUE(message.starts_with("@actor.lua: "));
        EXPECT_NE(message.find("module exploded"), std::string::npos);
        EXPECT_NE(message.find("stack traceback:"), std::string::npos);
        EXPECT_NE(message.find("scripts/fault.module.lua:3:"), std::string::npos);
        EXPECT_NE(message.find("actor.lua:4:"), std::string::npos);
        EXPECT_NE(message.find("actor.lua:7:"), std::string::npos);
        for(int repeat = 0; repeat < 256; ++repeat) {
            ASSERT_TRUE(instance.value()->invoke(Script::Phase::Start, {}, {}));
            ASSERT_FALSE(instance.value()->invoke(Script::Phase::Update, {}, {}));
        }
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Stop, {}, {}));
    }

    TEST_F(ScriptModulesTest, InitializationErrorsTraceNestedRequireSources) {
        write("actor.lua", "local wrapper = require('scripts.wrapper')\nreturn {}\n");
        write(
            "scripts/wrapper.module.lua", "local fault = require('scripts.fault')\nreturn fault\n");
        for(const auto* source : {"error('initialization failed')\nreturn {}\n", "return {"}) {
            SCOPED_TRACE(source);
            write("scripts/fault.module.lua", source);
            const auto failed = load();
            ASSERT_FALSE(failed);
            const auto& message = failed.error().message;
            EXPECT_TRUE(message.starts_with("@actor.lua: "));
            EXPECT_NE(message.find("stack traceback:"), std::string::npos);
            EXPECT_NE(message.find("scripts/fault.module.lua:1:"), std::string::npos);
            EXPECT_NE(message.find("scripts/wrapper.module.lua:1:"), std::string::npos);
            EXPECT_NE(message.find("actor.lua:1:"), std::string::npos);
        }
    }

    TEST_F(ScriptModulesTest, GroupFreshnessIncludesOtherRootsButSourceEqualityDoesNot) {
        write("scripts/value.module.lua", "return {base = 7}");
        write("first.lua",
            "local value = require('scripts.value'); return {properties = {base = value.base}}");
        write("second.lua", "return {properties = {value = 1}}");
        const std::array<std::filesystem::path, 2> paths{"second.lua", "first.lua"};
        const auto group = Script::load_group(directory.path(), paths);
        ASSERT_TRUE(group) << group.error().message;
        EXPECT_EQ(group.value()[0]->source_path(), "second.lua");
        EXPECT_EQ(group.value()[1]->source_path(), "first.lua");
        const auto same = load("first.lua");
        ASSERT_TRUE(same);
        EXPECT_TRUE(group.value()[1]->has_same_sources(*same.value()[0]));
        write("second.lua", "return {properties = {value = 2}}");
        EXPECT_FALSE(group.value()[0]->inputs_are_current());
        EXPECT_FALSE(group.value()[1]->inputs_are_current());
        EXPECT_TRUE(same.value()[0]->inputs_are_current());
        const auto changed_group = Script::load_group(directory.path(), paths);
        ASSERT_TRUE(changed_group);
        EXPECT_FALSE(group.value()[0]->has_same_sources(*changed_group.value()[0]));
        EXPECT_TRUE(group.value()[1]->has_same_sources(*changed_group.value()[1]));
        write("scripts/value.module.lua", "return {base = 8}");
        const auto changed_module = load("first.lua");
        ASSERT_TRUE(changed_module);
        EXPECT_FALSE(group.value()[1]->has_same_sources(*changed_module.value()[0]));
        const auto memory = Script::create("return {}", "memory.lua");
        const auto memory_copy = Script::create("return {}", "memory.lua");
        const auto renamed = Script::create("return {}", "other.lua");
        ASSERT_TRUE(memory);
        ASSERT_TRUE(memory_copy);
        ASSERT_TRUE(renamed);
        EXPECT_TRUE(memory.value()->has_same_sources(*memory_copy.value()));
        EXPECT_FALSE(memory.value()->has_same_sources(*renamed.value()));
    }

    TEST_F(ScriptModulesTest, MissingDependenciesRemainObservableUntilTheirSourcesRecover) {
        write("actor.lua", "require('scripts.wrapper'); return {}");
        write("scripts/wrapper.module.lua", "return require('scripts.missing')");
        const auto missing = load();
        ASSERT_FALSE(missing);
        EXPECT_EQ(missing.error().dependencies.at("actor.lua"),
            (std::vector<std::filesystem::path>{
                "scripts/missing.module.lua", "scripts/wrapper.module.lua"}));
        EXPECT_TRUE(missing.error().inputs_are_current());
        write("scripts/missing.module.lua", "return {");
        EXPECT_FALSE(missing.error().inputs_are_current());
        const auto invalid = load();
        ASSERT_FALSE(invalid);
        EXPECT_TRUE(invalid.error().inputs_are_current());
        write("scripts/missing.module.lua", "return {}");
        EXPECT_FALSE(invalid.error().inputs_are_current());
        EXPECT_TRUE(load());
    }

    TEST_F(ScriptModulesTest, CollectsAttemptedDependenciesForEveryRootInAFailedGroup) {
        write("first.lua", "require('missing.first'); return {}");
        write("second.lua", "require('missing.second'); return {}");
        const std::array<std::filesystem::path, 2> paths{"first.lua", "second.lua"};
        const auto failed = Script::load_group(directory.path(), paths);
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.error().dependencies.at("first.lua"),
            std::vector<std::filesystem::path>{"missing/first.module.lua"});
        EXPECT_EQ(failed.error().dependencies.at("second.lua"),
            std::vector<std::filesystem::path>{"missing/second.module.lua"});
    }

    TEST_F(ScriptModulesTest, ReportsCyclesAndRejectsNonTableModuleExports) {
        write("actor.lua", "require('scripts.first'); return {}");
        write("scripts/first.module.lua", "return require('scripts.second')");
        write("scripts/second.module.lua", "return require('scripts.first')");
        const auto cycle = load();
        ASSERT_FALSE(cycle);
        EXPECT_NE(cycle.error().message.find("cycle"), std::string::npos);
        EXPECT_NE(cycle.error().message.find("scripts/first.module.lua"), std::string::npos);
        EXPECT_NE(cycle.error().message.find("scripts/second.module.lua"), std::string::npos);
        EXPECT_TRUE(cycle.error().inputs_are_current());
        for(const char* result : {"nil", "false", "42", "'text'", "function() end"}) {
            SCOPED_TRACE(result);
            write("scripts/second.module.lua", std::string("return ") + result);
            const auto failed = load();
            ASSERT_FALSE(failed);
            EXPECT_NE(failed.error().message.find("must return a table"), std::string::npos);
        }
        write("scripts/second.module.lua", "return {}");
        EXPECT_FALSE(cycle.error().inputs_are_current());
        EXPECT_TRUE(load());
    }

    TEST_F(ScriptModulesTest, RejectsUnsafeModuleNamesPathsAndModuleComponentRoots) {
        for(const char* name : {"''", "'../outside'", "'/tmp/outside'", "'scripts/value'",
                "'scripts..value'", "'.value'", "'scripts.'", "'1value'", "'C:value'",
                R"('scripts\\value')", R"('scripts.value\0hidden')", "string.rep('a', 257)", "42",
                "nil", "'scripts.value', 'extra'"}) {
            SCOPED_TRACE(name);
            write("actor.lua", std::string("require(") + name + "); return {}");
            const auto failed = load();
            ASSERT_FALSE(failed);
            EXPECT_TRUE(failed.error().dependencies.at("actor.lua").empty());
        }
        write("value.module.lua", "return {}");
        EXPECT_FALSE(Script::load(directory.path() / "value.module.lua"));
        EXPECT_FALSE(load("value.module.lua"));
        EXPECT_FALSE(load("../outside.lua"));
        EXPECT_FALSE(load(directory.path() / "value.module.lua"));
        write("actor.lua",
            "assert(package == nil and io == nil and os == nil and load == nil); return {}");
        EXPECT_TRUE(Script::load(directory.path() / "actor.lua"));
        EXPECT_TRUE(load());
    }

    TEST_F(ScriptModulesTest, SymlinkAliasesAreRejectedAndSnapshotRetargetingIsDetected) {
        TemporaryDirectory outside;
        ASSERT_TRUE(write_text_file_atomic(outside.path() / "value.module.lua", "return {}"));
        std::error_code error;
        std::filesystem::create_symlink(
            outside.path() / "value.module.lua", directory.path() / "escape.module.lua", error);
        if(error)
            GTEST_SKIP() << "Symbolic links are unavailable: " << error.message();
        write("actor.lua", "require('escape'); return {}");
        const auto escaped = load();
        ASSERT_FALSE(escaped);
        EXPECT_NE(escaped.error().message.find("outside project assets"), std::string::npos);
        EXPECT_TRUE(escaped.error().dependencies.at("actor.lua").empty());
        write("value.module.lua", "return {}");
        std::filesystem::create_symlink(
            directory.path() / "value.module.lua", directory.path() / "alias.module.lua", error);
        ASSERT_FALSE(error);
        write("actor.lua", "require('alias'); return {}");
        const auto alias = load();
        ASSERT_FALSE(alias);
        EXPECT_NE(alias.error().message.find("symlink aliases"), std::string::npos);
        EXPECT_TRUE(alias.error().dependencies.at("actor.lua").empty());
        write("actor.lua", "require('value'); return {}");
        const auto captured = load();
        ASSERT_TRUE(captured);
        EXPECT_TRUE(captured.value()[0]->inputs_are_current());
        ASSERT_TRUE(std::filesystem::remove(directory.path() / "value.module.lua"));
        std::filesystem::create_symlink(
            outside.path() / "value.module.lua", directory.path() / "value.module.lua", error);
        ASSERT_FALSE(error);
        EXPECT_FALSE(captured.value()[0]->inputs_are_current());
        EXPECT_TRUE(captured.value()[0]->instantiate());
    }

    TEST_F(ScriptModulesTest, RuntimeCannotDiscoverModulesEvenFromAnotherPreparedRoot) {
        write("value.module.lua", "return {}");
        write("first.lua", "require('value'); return {}");
        write("second.lua", "return {update = function() require('value') end}");
        const std::array<std::filesystem::path, 2> paths{"first.lua", "second.lua"};
        const auto group = Script::load_group(directory.path(), paths);
        ASSERT_TRUE(group);
        EXPECT_TRUE(group.value()[1]->dependencies().empty());
        const auto instance = group.value()[1]->instantiate();
        ASSERT_TRUE(instance);
        const auto result = instance.value()->invoke(Script::Phase::Update, {}, {});
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().message.find("not loaded during script initialization"),
            std::string::npos);
    }

    TEST_F(ScriptModulesTest, ModuleInitializationSharesInstructionAndMemoryBudgets) {
        write("actor.lua", "require('value'); return {}");
        for(const char* source :
            {"while true do end", "return {value = string.rep('x', 32*1024*1024)}"}) {
            SCOPED_TRACE(source);
            write("value.module.lua", source);
            const auto failed = load();
            ASSERT_FALSE(failed);
            EXPECT_TRUE(failed.error().inputs_are_current());
        }
    }

    TEST_F(ScriptModulesTest, ModuleCountAndDependencyDepthHaveExplicitBoundaries) {
        for(int count = 1; count <= 65; ++count)
            write("modules/m" + std::to_string(count) + ".module.lua", "return {}");
        for(const int count : {64, 65}) {
            write("actor.lua", "for i = 1, " + std::to_string(count)
                                   + " do require('modules.m' .. i) end; return {}");
            const auto group = load();
            EXPECT_EQ(static_cast<bool>(group), count == 64);
        }
        write("actor.lua", "require('chain.m1'); return {}");
        for(int count = 1; count < 16; ++count)
            write("chain/m" + std::to_string(count) + ".module.lua",
                "return require('chain.m" + std::to_string(count + 1) + "')");
        write("chain/m16.module.lua", "return {}");
        EXPECT_TRUE(load());
        write("chain/m16.module.lua", "return require('chain.m17')");
        write("chain/m17.module.lua", "return {}");
        const auto deep = load();
        ASSERT_FALSE(deep);
        EXPECT_NE(deep.error().message.find("depth exceeds 16"), std::string::npos);
    }

    TEST_F(ScriptModulesTest, SourceFileGroupByteAndRootBudgetsAreBounded) {
        std::string source = "return {} --";
        source.resize(1024 * 1024, ' ');
        write("actor.lua", source);
        EXPECT_TRUE(load());
        write("actor.lua", source + " ");
        const auto large = load();
        ASSERT_FALSE(large);
        EXPECT_NE(large.error().message.find("1 MiB"), std::string::npos);
        EXPECT_TRUE(large.error().inputs_are_current());
        write("actor.lua", "return {}");
        EXPECT_FALSE(large.error().inputs_are_current());
        std::vector<std::filesystem::path> paths;
        for(int count = 0; count < 9; ++count) {
            paths.emplace_back("large" + std::to_string(count) + ".lua");
            write(paths.back(), source);
        }
        EXPECT_TRUE(Script::load_group(directory.path(), std::span(paths).first(8)));
        const auto total = Script::load_group(directory.path(), paths);
        ASSERT_FALSE(total);
        EXPECT_NE(total.error().message.find("8 MiB"), std::string::npos);
        EXPECT_TRUE(total.error().inputs_are_current());
        write(paths.back(), "return {}");
        EXPECT_FALSE(total.error().inputs_are_current());
        paths.resize(129, "actor.lua");
        EXPECT_FALSE(Script::load_group(directory.path(), paths));
    }

    TEST(ScriptInvocationTest, RestartRequiresAnActiveRuntimeUpdateAndValidEntity) {
        const auto script = Script::create(R"(
            local function restart()
                comet.restart_scene()
                comet.session_set('after.restart', true)
            end
            return {on_start = restart, on_stop = restart, update = restart,
                fixed_update = restart, on_trigger_enter = restart,
                on_trigger_exit = restart, on_collision_enter = restart,
                on_collision_exit = restart}
        )");
        ASSERT_TRUE(script) << script.error().message;
        const auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        Scene scene;
        SceneRuntime runtime;
        const auto actor = scene.create_entity();
        InputState input;
        EXPECT_FALSE(instance.value()->invoke(Script::Phase::Update, actor, {},
            {.scene = &scene, .session = &runtime.get_session(), .input = &input}));
        EXPECT_FALSE(runtime.take_restart_request());
        ASSERT_TRUE(runtime.start(scene));
        for(const auto phase : {Script::Phase::Start, Script::Phase::Stop}) {
            EXPECT_FALSE(instance.value()->invoke(
                phase, actor, {}, {.scene = &scene, .session = &runtime.get_session()}));
            EXPECT_FALSE(runtime.take_restart_request());
            EXPECT_FALSE(instance.value()->invoke(phase, actor, {},
                {.scene = &scene, .session = &runtime.get_session(), .input = &input}));
            EXPECT_FALSE(runtime.take_restart_request());
        }
        for(const auto phase : {Script::Phase::Update, Script::Phase::FixedUpdate,
                Script::Phase::TriggerEnter, Script::Phase::TriggerExit,
                Script::Phase::CollisionEnter, Script::Phase::CollisionExit}) {
            const auto called = instance.value()->invoke(phase, actor, {},
                {.scene = &scene, .session = &runtime.get_session(), .contact_other = actor});
            ASSERT_TRUE(called) << called.error().message;
            EXPECT_TRUE(runtime.take_restart_request());
            EXPECT_TRUE(runtime.get_session().get_value("after.restart"));
        }
        EXPECT_FALSE(instance.value()->invoke(static_cast<Script::Phase>(-1), actor, {},
            {.scene = &scene, .session = &runtime.get_session(), .input = &input}));
        EXPECT_FALSE(runtime.take_restart_request());
        scene.destroy_entity(actor);
        EXPECT_FALSE(instance.value()->invoke(Script::Phase::Update, actor, {},
            {.scene = &scene, .session = &runtime.get_session(), .input = &input}));
        EXPECT_FALSE(runtime.take_restart_request());
        ASSERT_TRUE(runtime.stop());
    }

    TEST(ScriptInvocationTest, SessionAccessRequiresExplicitMatchingRuntime) {
        Scene scene;
        Scene other_scene;
        const auto actor = scene.create_entity();
        SceneRuntime runtime;
        SceneRuntime other_runtime;
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(other_runtime.start(other_scene));
        const auto script = Script::create(R"(return {
            update = function()
                comet.session_set('score', 3)
                comet.translate(1, 0, 0)
                comet.restart_scene()
            end
        })");
        ASSERT_TRUE(script);
        const auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        const auto mismatch = instance.value()->invoke(Script::Phase::Update, actor, {},
            {.scene = &scene, .session = &other_runtime.get_session()});
        ASSERT_FALSE(mismatch);
        EXPECT_NE(mismatch.error().message.find("another scene"), std::string::npos);
        EXPECT_FALSE(runtime.get_session().get_value("score"));
        EXPECT_FALSE(other_runtime.get_session().get_value("score"));
        EXPECT_EQ(actor.get_component<TransformComponent>().translation, Math::Vec3(0));
        EXPECT_FALSE(instance.value()->invoke(Script::Phase::Update, actor, {}, {.scene = &scene}));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Update, actor, {},
            {.scene = &scene, .session = &runtime.get_session()}));
        EXPECT_EQ(runtime.get_session().get_value("score"), ParameterValue(3.0f));
        EXPECT_TRUE(runtime.take_restart_request());
        EXPECT_FALSE(other_runtime.take_restart_request());
        // 一次合法调用不能给后续未提供会话的调用遗留权限。
        EXPECT_FALSE(instance.value()->invoke(Script::Phase::Update, actor, {}, {.scene = &scene}));
        EXPECT_EQ(actor.get_component<TransformComponent>().translation, Math::Vec3(1, 0, 0));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(instance.value()->invoke(Script::Phase::Update, actor, {},
            {.scene = &scene, .session = &runtime.get_session()}));
        EXPECT_FALSE(runtime.get_session().get_value("score"));
        EXPECT_TRUE(other_runtime.is_active());
    }

    TEST(ScriptInvocationTest, InputContextsValidateCallsAndFailUnknownGroupsAtTheRuntimeBoundary) {
        const auto script = Script::create(R"(return {
            on_start = function() comet.set_input_context('gameplay', false) end,
            on_stop = function() comet.set_input_context('gameplay', true) end,
            update = function()
                comet.set_input_context('missing', true)
                comet.session_set('before.failure', true)
            end
        })");
        ASSERT_TRUE(script) << script.error().message;
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        auto actions = InputActions::create(
            {{"move", InputActions::Type::Axis, {{Input::Key::L}}, "gameplay"}},
            {{"gameplay", true}});
        ASSERT_TRUE(actions);
        Scene scene;
        SceneRuntime runtime;
        const auto actor = scene.create_entity();
        ASSERT_TRUE(runtime.set_input_actions(std::move(actions).value()));
        EXPECT_FALSE(instance.value()->invoke(
            Script::Phase::Start, actor, {}, {.scene = &scene, .session = &runtime.get_session()}));
        ASSERT_TRUE(runtime.start(scene));
        std::vector<std::string> disabled_contexts;
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Start, actor, {},
            {.scene = &scene,
                .session = &runtime.get_session(),
                .disabled_input_contexts = &disabled_contexts}));
        EXPECT_TRUE(disabled_contexts.empty());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FALSE(instance.value()->invoke(Script::Phase::Stop, {}, {}));

        for(const char* arguments : {"42, true", "'', true", "'bad name', true",
                "string.rep('a', 65), true", "'gameplay', 1", "'gameplay', nil", "'gameplay'"}) {
            SCOPED_TRACE(arguments);
            const auto invalid =
                Script::create(std::string("return {update = function() ")
                               + "comet.set_input_context(" + arguments + ") end}");
            ASSERT_TRUE(invalid);
            auto invalid_instance = invalid.value()->instantiate();
            ASSERT_TRUE(invalid_instance);
            EXPECT_FALSE(invalid_instance.value()->invoke(Script::Phase::Update, actor, {},
                {.scene = &scene, .session = &runtime.get_session()}));
        }
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Update, actor, {},
            {.scene = &scene,
                .session = &runtime.get_session(),
                .disabled_input_contexts = &disabled_contexts}));
        EXPECT_TRUE(disabled_contexts.empty());
        EXPECT_TRUE(runtime.get_session().get_value("before.failure"));
        const auto failed = runtime.advance(0);
        ASSERT_FALSE(failed);
        EXPECT_NE(failed.error().message.find("missing"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_FALSE(runtime.get_session().get_value("before.failure"));
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_TRUE(runtime.advance(0));
        ASSERT_TRUE(runtime.stop());
    }

    TEST(ScriptInvocationTest, StopInputContextCleanupDeduplicatesAndBoundsNames) {
        const auto script = Script::create(R"(return {on_stop = function(self)
            if self.parameters.name then
                comet.set_input_context(self.parameters.name, false)
                comet.set_input_context(self.parameters.name, false)
                return
            end
            for i = 1, self.parameters.count do
                comet.set_input_context('context.' .. i, false)
                comet.set_input_context('context.' .. i, false)
            end
        end})");
        ASSERT_TRUE(script) << script.error().message;
        const auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        std::vector<std::string> disabled_contexts;
        const Script::Invocation cleanup{.disabled_input_contexts = &disabled_contexts};
        const auto boundary = std::string(64, 'a');
        ASSERT_TRUE(
            instance.value()->invoke(Script::Phase::Stop, {}, {{"name", boundary}}, cleanup));
        EXPECT_EQ(disabled_contexts, std::vector<std::string>{boundary});
        for(const auto& name : {std::string{}, std::string("bad name"), std::string(65, 'a'),
                std::string("palette\0hidden", 14)}) {
            SCOPED_TRACE(name);
            disabled_contexts.clear();
            EXPECT_FALSE(
                instance.value()->invoke(Script::Phase::Stop, {}, {{"name", name}}, cleanup));
            EXPECT_TRUE(disabled_contexts.empty());
        }

        std::vector<std::string> expected;
        for(std::size_t i = 1; i <= InputActions::MAX_CONTEXTS; ++i)
            expected.push_back("context." + std::to_string(i));
        for(const std::size_t count :
            {InputActions::MAX_CONTEXTS, InputActions::MAX_CONTEXTS + 1}) {
            SCOPED_TRACE(count);
            disabled_contexts.clear();
            const auto stopped = instance.value()->invoke(
                Script::Phase::Stop, {}, {{"count", static_cast<float>(count)}}, cleanup);
            EXPECT_EQ(static_cast<bool>(stopped), count == InputActions::MAX_CONTEXTS);
            EXPECT_EQ(disabled_contexts, expected);
        }
    }

    TEST(ScriptInvocationTest, StopInputContextCleanupRejectsEnableAndKeepsAcceptedOutput) {
        const auto script = Script::create(R"(return {on_stop = function(self)
            comet.set_input_context('palette', false)
            comet.set_input_context('palette', self.parameters.enable)
            error('cleanup failed after disable')
        end})");
        ASSERT_TRUE(script) << script.error().message;
        const auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        for(const bool enable : {true, false}) {
            SCOPED_TRACE(enable);
            std::vector<std::string> disabled_contexts;
            const auto stopped = instance.value()->invoke(Script::Phase::Stop, {},
                {{"enable", enable}}, {.disabled_input_contexts = &disabled_contexts});
            ASSERT_FALSE(stopped);
            EXPECT_EQ(disabled_contexts, std::vector<std::string>{"palette"});
            if(enable)
                EXPECT_NE(stopped.error().message.find("only disable"), std::string::npos);
            else
                EXPECT_NE(stopped.error().message.find("cleanup failed after disable"),
                    std::string::npos);
        }
    }

    TEST(ScriptInvocationTest, StopInvocationCannotBorrowSceneOrInputCapabilities) {
        Scene scene;
        SceneRuntime runtime;
        const auto actor = scene.create_entity();
        const auto actions =
            InputActions::create({{"test", InputActions::Type::Button, {{Input::Key::Space}}}});
        ASSERT_TRUE(actions);
        InputState input;
        actions.value().evaluate({}, input);
        ASSERT_NE(input.action("test"), nullptr);
        ASSERT_TRUE(runtime.start(scene));
        for(const char* operation : {"comet.translate(1, 0, 0)", "self.target:translate(1, 0, 0)",
                "comet.session_set('leaked', true)", "comet.emit('leaked')",
                "comet.restart_scene()", "comet.action_down('test')"}) {
            SCOPED_TRACE(operation);
            const auto script = Script::create(
                std::string(
                    "return {on_start = function(self) self.target = comet.self_entity() end,")
                + "on_stop = function(self) comet.set_input_context('palette', false); " + operation
                + " end}");
            ASSERT_TRUE(script) << script.error().message;
            const auto instance = script.value()->instantiate();
            ASSERT_TRUE(instance);
            ASSERT_TRUE(instance.value()->invoke(Script::Phase::Start, actor, {},
                {.scene = &scene, .session = &runtime.get_session()}));
            std::vector<std::string> disabled_contexts;
            const auto stopped = instance.value()->invoke(Script::Phase::Stop, actor, {},
                {.scene = &scene,
                    .session = &runtime.get_session(),
                    .input = &input,
                    .disabled_input_contexts = &disabled_contexts});
            ASSERT_FALSE(stopped);
            EXPECT_EQ(disabled_contexts, std::vector<std::string>{"palette"});
            EXPECT_EQ(actor.get_component<TransformComponent>().translation, Math::Vec3(0));
            EXPECT_FALSE(runtime.get_session().get_value("leaked"));
            EXPECT_FALSE(runtime.take_restart_request());
        }
        // 输出只属于调用方；未配置 palette 的 Runtime 不应收到这批关闭请求。
        EXPECT_TRUE(runtime.advance(0));
        ASSERT_TRUE(runtime.stop());
    }

    TEST(ScriptInvocationTest, ImpulsesRequireFiniteArgumentsAndALiveDynamicBody) {
        const auto script = Script::create(R"(
            local function impulse() comet.apply_impulse(0, 1, 0) end
            return {on_start = impulse, on_stop = impulse, update = impulse,
                fixed_update = impulse, on_trigger_enter = impulse,
                on_trigger_exit = impulse, on_collision_enter = impulse,
                on_collision_exit = impulse}
        )");
        ASSERT_TRUE(script) << script.error().message;
        const auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        Scene scene;
        PhysicsService physics;
        auto actor = scene.create_entity();
        actor.add_component<RigidBodyComponent>();
        actor.add_component<ColliderComponent>();
        EXPECT_FALSE(instance.value()->invoke(
            Script::Phase::Start, actor, {}, {.scene = &scene, .physics = &physics}));
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.start(scene));
        for(const auto phase : {Script::Phase::Start, Script::Phase::FixedUpdate,
                Script::Phase::Update, Script::Phase::CollisionEnter, Script::Phase::CollisionExit,
                Script::Phase::TriggerEnter, Script::Phase::TriggerExit}) {
            const auto called = instance.value()->invoke(
                phase, actor, {}, {.scene = &scene, .physics = &physics, .contact_other = actor});
            ASSERT_TRUE(called) << called.error().message;
        }
        EXPECT_FALSE(instance.value()->invoke(Script::Phase::Stop, {}, {}));
        EXPECT_FALSE(instance.value()->invoke(
            Script::Phase::Update, {}, {}, {.scene = &scene, .physics = &physics}));
        for(const auto motion : {BodyMotion::Static, BodyMotion::Kinematic}) {
            actor.get_component<RigidBodyComponent>().motion = motion;
            EXPECT_FALSE(instance.value()->invoke(
                Script::Phase::Update, actor, {}, {.scene = &scene, .physics = &physics}));
        }
        actor.get_component<RigidBodyComponent>().motion = BodyMotion::Dynamic;
        actor.remove_component<ColliderComponent>();
        EXPECT_FALSE(instance.value()->invoke(
            Script::Phase::Update, actor, {}, {.scene = &scene, .physics = &physics}));
        actor.add_component<ColliderComponent>();
        actor.remove_component<RigidBodyComponent>();
        EXPECT_FALSE(instance.value()->invoke(
            Script::Phase::Update, actor, {}, {.scene = &scene, .physics = &physics}));
        actor.add_component<RigidBodyComponent>();
        for(const char* arguments : {"", "0, 1", "0, 1, 0, 1", "'0', 1, 0", "0, true, 0",
                "0, nil, 0", "0, {}, 0", "0, math.huge, 0", "0, 0/0, 0", "0, 1e40, 0"}) {
            SCOPED_TRACE(arguments);
            const auto invalid = Script::create(std::string("return {update = function() ")
                                                + "comet.apply_impulse(" + arguments + ") end}");
            ASSERT_TRUE(invalid);
            auto invalid_instance = invalid.value()->instantiate();
            ASSERT_TRUE(invalid_instance);
            EXPECT_FALSE(invalid_instance.value()->invoke(
                Script::Phase::Update, actor, {}, {.scene = &scene, .physics = &physics}));
        }
        scene.destroy_entity(actor);
        EXPECT_FALSE(instance.value()->invoke(
            Script::Phase::Update, actor, {}, {.scene = &scene, .physics = &physics}));
        ASSERT_TRUE(runtime.stop());
    }

    TEST(ScriptInvocationTest, RigidBodyRemovalIsDeferredAndPreservesTheTargetEntity) {
        Scene scene;
        const auto actor = scene.create_entity("Actor");
        auto target = scene.create_entity("Target");
        target.add_component<RigidBodyComponent>();
        target.add_component<ColliderComponent>().is_trigger = true;
        target.add_component<MeshRendererComponent>(AssetHandle{11}, AssetHandle{12});
        const auto uuid = target.get_uuid();
        const auto id = target.get_id();
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.start(scene));
        const auto script = Script::create(R"(return {
            properties = {target = {type = 'entity'}},
            on_start = function(self)
                assert(not comet.has_rigid_body(comet.self_entity()))
                assert(comet.has_rigid_body(self.parameters.target))
                comet.remove_rigid_body(self.parameters.target)
                comet.remove_rigid_body(self.parameters.target)
                assert(comet.has_rigid_body(self.parameters.target))
            end,
            update = function(self)
                assert(self.parameters.target:is_valid())
                assert(not comet.has_rigid_body(self.parameters.target))
                comet.remove_rigid_body(self.parameters.target)
                comet.remove_rigid_body(self.parameters.target)
                assert(not comet.has_rigid_body(self.parameters.target))
            end,
        })");
        ASSERT_TRUE(script) << script.error().message;
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        const ParameterMap parameters{{"target", uuid}};
        const auto requested =
            instance.value()->invoke(Script::Phase::Start, actor, parameters, {.scene = &scene});
        ASSERT_TRUE(requested) << requested.error().message;
        EXPECT_TRUE(target.has_component<RigidBodyComponent>());
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(target);
        EXPECT_EQ(target.get_id(), id);
        EXPECT_EQ(target.get_uuid(), uuid);
        EXPECT_FALSE(target.has_component<RigidBodyComponent>());
        ASSERT_TRUE(target.has_component<ColliderComponent>());
        EXPECT_TRUE(target.get_component<ColliderComponent>().is_trigger);
        ASSERT_TRUE(target.has_component<MeshRendererComponent>());
        EXPECT_EQ(target.get_component<MeshRendererComponent>().mesh, AssetHandle{11});
        EXPECT_EQ(target.get_component<MeshRendererComponent>().material, AssetHandle{12});
        EXPECT_EQ(target.get_component<NameComponent>().name, "Target");
        const auto repeated =
            instance.value()->invoke(Script::Phase::Update, actor, parameters, {.scene = &scene});
        ASSERT_TRUE(repeated) << repeated.error().message;
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(scene.entity_count(), 2u);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(ScriptInvocationTest, RigidBodyOperationsValidateSceneAccessAndTypedReferences) {
        for(const char* operation : {"has_rigid_body", "remove_rigid_body"}) {
            SCOPED_TRACE(operation);
            const bool read_only = std::string_view(operation) == "has_rigid_body";
            Scene scene;
            auto actor = scene.create_entity();
            actor.add_component<RigidBodyComponent>();
            const auto script = Script::create(
                std::string("local function access(self) comet.") + operation + R"((self.target) end
                    return {
                        on_start = function(self)
                            self.target = comet.self_entity()
                            access(self)
                        end,
                        update = access,
                        on_stop = access,
                    })");
            ASSERT_TRUE(script) << script.error().message;
            auto instance = script.value()->instantiate();
            ASSERT_TRUE(instance);
            EXPECT_EQ(static_cast<bool>(instance.value()->invoke(
                          Script::Phase::Start, actor, {}, {.scene = &scene})),
                read_only);
            EXPECT_TRUE(actor.has_component<RigidBodyComponent>());
            SceneRuntime runtime;
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(
                instance.value()->invoke(Script::Phase::Start, actor, {}, {.scene = &scene}));
            EXPECT_FALSE(instance.value()->invoke(Script::Phase::Stop, {}, {}));
            for(const char* argument : {"", "nil", "false", "42", "'entity'", "{}"}) {
                SCOPED_TRACE(argument);
                const auto invalid =
                    Script::create(std::string("return {update = function() comet.") + operation
                                   + "(" + argument + ") end}");
                ASSERT_TRUE(invalid);
                auto invalid_instance = invalid.value()->instantiate();
                ASSERT_TRUE(invalid_instance);
                EXPECT_FALSE(invalid_instance.value()->invoke(
                    Script::Phase::Update, actor, {}, {.scene = &scene}));
            }
            ASSERT_TRUE(runtime.stop());
            EXPECT_TRUE(actor.has_component<RigidBodyComponent>());
            const auto stale =
                instance.value()->invoke(Script::Phase::Update, actor, {}, {.scene = &scene});
            ASSERT_FALSE(stale);
            EXPECT_NE(stale.error().message.find("stale"), std::string::npos);
            EXPECT_EQ(static_cast<bool>(instance.value()->invoke(
                          Script::Phase::Start, actor, {}, {.scene = &scene})),
                read_only);
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(runtime.advance(0));
            EXPECT_TRUE(actor.has_component<RigidBodyComponent>());
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST(ScriptInvocationTest, RigidBodyOperationsRejectRecreatedAndForeignEntityReferences) {
        for(const char* operation : {"has_rigid_body", "remove_rigid_body"}) {
            SCOPED_TRACE(operation);
            Scene first;
            auto original = first.create_entity();
            original.add_component<RigidBodyComponent>();
            const auto uuid = original.get_uuid();
            const auto id = original.get_id();
            SceneRuntime first_runtime;
            ASSERT_TRUE(first_runtime.start(first));
            const auto script = Script::create(
                std::string(
                    "return {on_start = function(self) self.target = comet.self_entity() end,")
                + "update = function(self) comet." + operation + "(self.target) end}");
            ASSERT_TRUE(script) << script.error().message;
            auto instance = script.value()->instantiate();
            ASSERT_TRUE(instance);
            ASSERT_TRUE(
                instance.value()->invoke(Script::Phase::Start, original, {}, {.scene = &first}));
            first.destroy_entity(original);
            auto replacement = first.create_entity_with_uuid(uuid);
            replacement.add_component<RigidBodyComponent>();
            ASSERT_NE(replacement.get_id(), id);
            auto result =
                instance.value()->invoke(Script::Phase::Update, replacement, {}, {.scene = &first});
            ASSERT_FALSE(result);
            EXPECT_NE(result.error().message.find("stale"), std::string::npos);
            ASSERT_TRUE(first_runtime.advance(0));
            EXPECT_TRUE(replacement.has_component<RigidBodyComponent>());

            ASSERT_TRUE(
                instance.value()->invoke(Script::Phase::Start, replacement, {}, {.scene = &first}));
            Scene second;
            auto foreign = second.create_entity_with_uuid(uuid);
            foreign.add_component<RigidBodyComponent>();
            SceneRuntime second_runtime;
            ASSERT_TRUE(second_runtime.start(second));
            result =
                instance.value()->invoke(Script::Phase::Update, foreign, {}, {.scene = &second});
            ASSERT_FALSE(result);
            EXPECT_NE(result.error().message.find("stale"), std::string::npos);
            ASSERT_TRUE(second_runtime.advance(0));
            ASSERT_TRUE(first_runtime.advance(0));
            EXPECT_TRUE(foreign.has_component<RigidBodyComponent>());
            EXPECT_TRUE(replacement.has_component<RigidBodyComponent>());
            ASSERT_TRUE(second_runtime.stop());
            ASSERT_TRUE(first_runtime.stop());
        }
    }

    TEST(ScriptSourceTest, EntityPropertiesRequireAnExplicitTypeAndSceneOwnedTarget) {
        auto script = Script::create("return {properties = {target = {type = 'entity'}}}");
        ASSERT_TRUE(script) << script.error().message;
        EXPECT_EQ(std::get<EntityUuid>(script.value()->properties().at("target").default_value),
            EntityUuid{});
        EXPECT_EQ(script.value()->properties().at("target").semantic,
            Script::Property::Semantic::Default);
        EXPECT_TRUE(script.value()->validate_overrides({{"target", EntityUuid::generate()}}));
        EXPECT_FALSE(script.value()->validate_overrides({{"target", std::string("uuid")}}));
        EXPECT_FALSE(Script::create("return {properties = {target = {type = 'unknown'}}}"));
        EXPECT_FALSE(
            Script::create(R"(return {properties = {target = {type = 'entity\0extra'}}})"));
        EXPECT_FALSE(
            Script::create("return {properties = {target = {type = 'entity', default = 'uuid'}}}"));
    }

    TEST(ScriptInvocationTest, TypedReferencesCompareByEntityAndRespectLifetimeAndScene) {
        auto script = Script::create(R"(
            local script = {properties = {target = {type = 'entity'}}}
            function script:on_start()
                self.saved = self.parameters.target
                assert(self.saved:is_valid())
                assert(self.saved ~= comet.self_entity())
            end
            function script:on_trigger_enter(other)
                assert(self.parameters.target == other)
            end
            function script:update(dt)
                if dt == 1 then
                    assert(not self.parameters.target:is_valid())
                else
                    assert(not self.saved:is_valid())
                    assert(self.parameters.target:is_valid())
                    assert(self.saved ~= self.parameters.target)
                    self.parameters.target:translate(1, 0, 0)
                end
            end
            return script
        )");
        ASSERT_TRUE(script) << script.error().message;
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        Scene scene;
        const auto actor = scene.create_entity();
        const auto target = scene.create_entity("Target");
        const auto uuid = target.get_uuid();
        const ParameterMap parameters{{"target", uuid}};
        ASSERT_TRUE(
            instance.value()->invoke(Script::Phase::Start, actor, parameters, {.scene = &scene}));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::TriggerEnter, actor, parameters,
            {.scene = &scene, .contact_other = target}));
        scene.destroy_entity(target);
        const auto replacement = scene.create_entity_with_uuid(uuid);
        ASSERT_TRUE(instance.value()->invoke(
            Script::Phase::Update, actor, parameters, {.delta_time = 1, .scene = &scene}));
        EXPECT_EQ(replacement.get_component<TransformComponent>().translation, Math::Vec3(0));
        Scene other;
        const auto other_actor = other.create_entity();
        const auto other_target = other.create_entity_with_uuid(uuid);
        ASSERT_TRUE(instance.value()->invoke(
            Script::Phase::Update, other_actor, parameters, {.scene = &other}));
        EXPECT_EQ(
            other_target.get_component<TransformComponent>().translation, Math::Vec3(1, 0, 0));
    }

    TEST(ScriptInvocationTest, UnassignedAndMissingEntityParametersAreSafeInvalidReferences) {
        auto script = Script::create(R"(return {
            update = function(self) assert(not self.parameters.target:is_valid()) end,
            on_start = function(self) self.parameters.target:translate(1, 0, 0) end
        })");
        ASSERT_TRUE(script);
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        Scene scene;
        const auto actor = scene.create_entity();
        for(const auto target : {EntityUuid{}, EntityUuid::generate()}) {
            const ParameterMap parameters{{"target", target}};
            ASSERT_TRUE(instance.value()->invoke(
                Script::Phase::Update, actor, parameters, {.scene = &scene}));
            const auto invalid = instance.value()->invoke(
                Script::Phase::Start, actor, parameters, {.scene = &scene});
            ASSERT_FALSE(invalid);
            EXPECT_NE(invalid.error().message.find("stale"), std::string::npos);
        }
    }

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
        EXPECT_EQ(std::get<float>(script.value()->properties().at("speed").default_value), 1);
        EXPECT_FALSE(script.value()->validate_overrides({{"missing", 2.0f}}));
        EXPECT_FALSE(script.value()->resolve_parameters({{"speed", false}}));
    }

    TEST(ScriptSourceTest, CompatibleFilteringRemovesOnlyMissingNamesAndChangedStorageTypes) {
        const auto script = Script::create(R"(return {properties = {
            speed = 1, enabled = true, direction = {1, 2, 3}, changed = false, added = 9
        }})");
        ASSERT_TRUE(script) << script.error().message;
        const ParameterMap compatible{
            {"speed", 6.0f}, {"enabled", false}, {"direction", Math::Vec3(4, 5, 6)}};
        auto overrides = compatible;
        overrides.emplace("removed", std::string("old value"));
        overrides.emplace("changed", 3.0f);
        EXPECT_FALSE(script.value()->validate_overrides(overrides));

        script.value()->retain_compatible_overrides(overrides);

        EXPECT_EQ(overrides, compatible);
        EXPECT_TRUE(script.value()->validate_overrides(overrides));
        const auto resolved = script.value()->resolve_parameters(overrides);
        ASSERT_TRUE(resolved) << resolved.error().message;
        EXPECT_FALSE(std::get<bool>(resolved.value().at("changed")));
        EXPECT_FLOAT_EQ(std::get<float>(resolved.value().at("added")), 9);
        EXPECT_FLOAT_EQ(std::get<float>(script.value()->properties().at("speed").default_value), 1);
    }

    TEST(ScriptSourceTest, CompatibleFilteringPreservesColorVectorAndEntityOverridesIdempotently) {
        const auto previous = Script::create(R"(return {properties = {
            tint = {type = 'color', default = {1, 1, 1, 1}},
            vector = {0, 0, 0, 0}, target = {type = 'entity'}
        }})");
        const auto current = Script::create(R"(return {properties = {
            tint = {0, 0, 0, 0},
            vector = {type = 'color', default = {1, 1, 1, 1}}, target = {type = 'entity'}
        }})");
        ASSERT_TRUE(previous) << previous.error().message;
        ASSERT_TRUE(current) << current.error().message;
        const ParameterMap compatible{{"tint", Math::Vec4(2, -1, 0.5f, 0.25f)},
            {"vector", Math::Vec4(3, 4, 5, 6)}, {"target", EntityUuid::generate()}};
        ASSERT_TRUE(previous.value()->validate_overrides(compatible));
        auto overrides = compatible;

        current.value()->retain_compatible_overrides(overrides);
        EXPECT_EQ(overrides, compatible);
        EXPECT_TRUE(current.value()->validate_overrides(overrides));
        current.value()->retain_compatible_overrides(overrides);
        EXPECT_EQ(overrides, compatible);
    }

    TEST(ScriptSourceTest, CompatibleFilteringDoesNotSilentlyDiscardInvalidValues) {
        const auto script = Script::create(R"(return {properties = {
            speed = 1, direction = {0, 0, 0},
            tint = {type = 'color', default = {1, 1, 1, 1}}, label = ''
        }})");
        ASSERT_TRUE(script) << script.error().message;
        const auto infinity = std::numeric_limits<float>::infinity();
        for(const ParameterMap& invalid : {ParameterMap{{"speed", infinity}},
                ParameterMap{{"direction", Math::Vec3(0, infinity, 0)}},
                ParameterMap{{"tint", Math::Vec4(0, 0, infinity, 1)}},
                ParameterMap{{"label", std::string(4097, 'x')}}}) {
            SCOPED_TRACE(invalid.begin()->first);
            auto overrides = invalid;
            overrides.emplace("removed", true);

            script.value()->retain_compatible_overrides(overrides);

            EXPECT_EQ(overrides, invalid);
            EXPECT_FALSE(script.value()->validate_overrides(overrides));
            EXPECT_FALSE(script.value()->resolve_parameters(overrides));
        }
    }

    TEST(ScriptSourceTest, ColorsRequireAnExplicitSemanticWhileVectorSizesStayDistinct) {
        const auto script = Script::create(R"(return {properties = {
            direction = {1, 2, 3},
            color_named_vector = {0.2, 1, 0.25, 0.5},
            tint = {type = 'color', default = {-0.25, 2, 0.5, 1.5}},
        }})");
        ASSERT_TRUE(script) << script.error().message;
        const auto& properties = script.value()->properties();
        ASSERT_EQ(properties.size(), 3);
        EXPECT_EQ(
            std::get<Math::Vec3>(properties.at("direction").default_value), Math::Vec3(1, 2, 3));
        EXPECT_EQ(properties.at("direction").semantic, Script::Property::Semantic::Default);
        EXPECT_EQ(std::get<Math::Vec4>(properties.at("color_named_vector").default_value),
            Math::Vec4(0.2f, 1, 0.25f, 0.5f));
        EXPECT_EQ(
            properties.at("color_named_vector").semantic, Script::Property::Semantic::Default);
        EXPECT_EQ(std::get<Math::Vec4>(properties.at("tint").default_value),
            Math::Vec4(-0.25f, 2, 0.5f, 1.5f));
        EXPECT_EQ(properties.at("tint").semantic, Script::Property::Semantic::Color);

        const auto resolved = script.value()->resolve_parameters(
            {{"tint", Math::Vec4(4, 3, 2, 0.25f)}, {"color_named_vector", Math::Vec4(1, 2, 3, 4)}});
        ASSERT_TRUE(resolved) << resolved.error().message;
        EXPECT_EQ(std::get<Math::Vec4>(resolved.value().at("tint")), Math::Vec4(4, 3, 2, 0.25f));
        EXPECT_EQ(std::get<Math::Vec4>(resolved.value().at("color_named_vector")),
            Math::Vec4(1, 2, 3, 4));
        EXPECT_EQ(std::get<Math::Vec3>(resolved.value().at("direction")), Math::Vec3(1, 2, 3));
        EXPECT_EQ(std::get<Math::Vec4>(properties.at("tint").default_value),
            Math::Vec4(-0.25f, 2, 0.5f, 1.5f));
        EXPECT_EQ(properties.at("tint").semantic, Script::Property::Semantic::Color);
        EXPECT_FALSE(script.value()->resolve_parameters({{"tint", Math::Vec3(1)}}));
        EXPECT_FALSE(script.value()->resolve_parameters({{"color_named_vector", Math::Vec3(1)}}));
        EXPECT_FALSE(script.value()->resolve_parameters({{"direction", Math::Vec4(1)}}));
        EXPECT_FALSE(script.value()->resolve_parameters({{"tint", false}}));
        EXPECT_FALSE(script.value()->resolve_parameters(
            {{"tint", Math::Vec4(0, 0, 0, std::numeric_limits<float>::infinity())}}));
    }

    TEST(ScriptSourceTest, ColorDeclarationsRejectMissingDefaultsAndAdditionalMetadata) {
        for(const char* invalid : {"{type = 'color'}", "{type = 'color', default = false}",
                "{type = 'color', default = 'red'}", "{type = 'Color', default = {1, 2, 3, 4}}",
                "{type = true, default = {1, 2, 3, 4}}", "{type = 'vec4', default = {1, 2, 3, 4}}",
                "{type = 'color', default = {1, 2, 3, 4}, min = 0}",
                "{type = 'color', default = {1, 2, 3, 4}, [1] = 0}",
                R"({type = 'color\0extra', default = {1, 2, 3, 4}})",
                R"({type = 'color', default = {1, 2, 3, 4}, ['default\0'] = true})"}) {
            SCOPED_TRACE(invalid);
            const auto script =
                Script::create(std::string("return {properties = {tint = ") + invalid + "}}");
            EXPECT_FALSE(script);
        }
    }

    TEST(ScriptSourceTest, VectorAndColorArraysRejectHolesMixedKeysAndNonFiniteComponents) {
        for(const char* invalid : {"{}", "{1, 2}", "{1, 2, 3, 4, 5}", "{[1] = 1, [2] = 2, [4] = 4}",
                "{1, 2, 3, 4, [6] = 6}", "{1, 2, 3, 4, label = 'rgba'}", "{1, 2, 3, 4, [0] = 0}",
                "{1, 2, 3, 4, [-1] = 0}", "{1, 2, 3, 4, [2.5] = 0}", "{1, 2, 3, 4, [true] = 0}",
                "{1, 2, 3, '4'}", "{1, 2, 3, false}", "{1, 2, 3, math.huge}",
                "{1, 2, 3, -math.huge}", "{1, 2, 3, 0/0}", "{1, 2, 3, 1e39}"}) {
            SCOPED_TRACE(invalid);
            EXPECT_FALSE(
                Script::create(std::string("return {properties = {value = ") + invalid + "}}"));
            EXPECT_FALSE(Script::create(
                std::string("return {properties = {value = {type = 'color', default = ") + invalid
                + "}}}"));
        }
        EXPECT_FALSE(Script::create(
            "return {properties = {value = {type = 'color', default = {1, 2, 3}}}}"));
        EXPECT_FALSE(Script::create("return {properties = {value = {1, 2, 3, label = 'xyz'}}}"));
        EXPECT_FALSE(Script::create("return {properties = {value = {1, 2, 3, [0] = 0}}}"));
    }

    TEST(ScriptInvocationTest, FourComponentParametersSupportIndexLengthAndPairsAfterOverrides) {
        const auto script = Script::create(R"(return {
            properties = {
                tint = {type = 'color', default = {1, 2, 3, 0.5}},
                vector = {4, 5, 6, 7},
            },
            update = function(self, alpha)
                local tint = self.parameters.tint
                assert(#tint == 4 and tint[1] == 1 and tint[2] == 2 and tint[3] == 3)
                assert(tint[4] == alpha and tint[5] == nil and tint.type == nil and tint.default == nil)
                local count = 0
                for key, value in pairs(tint) do
                    assert(key >= 1 and key <= 4 and value == tint[key])
                    count = count + 1
                end
                assert(count == 4)
                assert(#self.parameters.vector == 4 and self.parameters.vector[4] == 7)
                if self.saved then
                    assert(self.saved[4] == 0.5)
                    assert(self.saved ~= tint)
                else
                    self.saved = tint
                end
            end,
        })");
        ASSERT_TRUE(script) << script.error().message;
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        const auto defaults = script.value()->resolve_parameters({});
        ASSERT_TRUE(defaults);
        ASSERT_TRUE(instance.value()->invoke(
            Script::Phase::Update, {}, defaults.value(), {.delta_time = 0.5}));
        const auto overridden =
            script.value()->resolve_parameters({{"tint", Math::Vec4(1, 2, 3, 0.25f)}});
        ASSERT_TRUE(overridden);
        ASSERT_TRUE(instance.value()->invoke(
            Script::Phase::Update, {}, overridden.value(), {.delta_time = 0.25}));
    }

    TEST(ScriptInvocationTest, FourComponentParametersRejectTopLevelAndNestedWrites) {
        for(const char* mutation : {"self.parameters.tint[4] = 0", "self.parameters.vector[4] = 0",
                "self.parameters.tint = {0, 0, 0, 0}", "table.insert(self.parameters.tint, 5)",
                "self.parameters.tint.label = 'changed'"}) {
            SCOPED_TRACE(mutation);
            const auto source = std::string(R"(return {
                properties = {tint = {type = 'color', default = {1, 2, 3, 4}}, vector = {1, 2, 3, 4}},
                update = function(self) )")
                                + mutation + " end}";
            const auto script = Script::create(source);
            ASSERT_TRUE(script) << script.error().message;
            auto instance = script.value()->instantiate();
            ASSERT_TRUE(instance);
            const auto parameters = script.value()->resolve_parameters({});
            ASSERT_TRUE(parameters);
            const auto result =
                instance.value()->invoke(Script::Phase::Update, {}, parameters.value());
            ASSERT_FALSE(result);
            EXPECT_NE(result.error().message.find("read-only"), std::string::npos);
        }
    }

    TEST(ScriptInvocationTest, FourComponentParametersDoNotExpandTheSessionValueDomain) {
        Scene scene;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.get_session().set_value("value", Math::Vec3(1, 2, 3)));
        EXPECT_FALSE(runtime.get_session().set_value("value", Math::Vec4(1, 2, 3, 4)));
        EXPECT_EQ(
            std::get<Math::Vec3>(*runtime.get_session().get_value("value")), Math::Vec3(1, 2, 3));
        const auto script = Script::create(R"(return {
            update = function()
                local value = comet.session_get('value')
                assert(#value == 3 and value[3] == 3)
                comet.session_set('value', {1, 2, 3, 4})
            end,
        })");
        ASSERT_TRUE(script);
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        const auto result = instance.value()->invoke(
            Script::Phase::Update, {}, {}, {.scene = &scene, .session = &runtime.get_session()});
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().message.find("three finite numbers"), std::string::npos);
        EXPECT_EQ(
            std::get<Math::Vec3>(*runtime.get_session().get_value("value")), Math::Vec3(1, 2, 3));
        ASSERT_TRUE(runtime.stop());
    }

    TEST(ScriptInvocationTest, SessionVectorsRejectInvalidShapeWithoutReplacingPreviousValue) {
        for(const char* vector : {"{}", "{1, 2}", "{1, 2, 3, 4}", "{[1]=1, [3]=3}",
                "{[0]=0, 1, 2, 3}", "{1, 2, 3, extra=4}", "{[1.5]=1, [2]=2, [3]=3}", "{1, '2', 3}",
                "{1, true, 3}", "{1, math.huge, 3}", "{1, 0/0, 3}", "{1, 1e100, 3}"}) {
            SCOPED_TRACE(vector);
            Scene scene;
            SceneRuntime runtime;
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(runtime.get_session().set_value("value", Math::Vec3(4, 5, 6)));
            const auto script = Script::create(
                std::string("return {update = function() comet.session_set('value', ") + vector
                + ") end}");
            ASSERT_TRUE(script) << script.error().message;
            auto instance = script.value()->instantiate();
            ASSERT_TRUE(instance);
            EXPECT_FALSE(instance.value()->invoke(Script::Phase::Update, {}, {},
                {.scene = &scene, .session = &runtime.get_session()}));
            EXPECT_EQ(std::get<Math::Vec3>(*runtime.get_session().get_value("value")),
                Math::Vec3(4, 5, 6));
        }
    }

    TEST(ScriptInvocationTest, MethodsUseInstanceStateWithoutReplacingDefinitionCallbacks) {
        const auto script = Script::create(R"(
            local group = {base = 4, events = {['test.step'] = 'on_step'}}
            function group:add(amount)
                self.total = (self.total or self.base) + amount
                return self.total
            end
            function group:twice(amount)
                local first = self:add(amount)
                return first, self:add(amount)
            end
            function group:on_start()
                assert(self ~= group)
                local first, last = self:twice(3)
                assert(first == 7 and last == 10)
                self.update = function() error('instance update shadow called') end
                self.on_step = function() error('instance event shadow called') end
                self.base = 8
                assert(group.base == 4)
                self.base = nil
                assert(self.base == 4)
            end
            function group:update(dt) assert(self:add(dt) == 12) end
            function group:on_step(value) assert(self:add(value) == 17) end
            function group:on_stop() assert(self.total == 17) end
            return group
        )");
        ASSERT_TRUE(script) << script.error().message;
        const auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance) << instance.error().message;
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Start, {}, {}));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Update, {}, {}, {.delta_time = 2}));
        const ParameterValue event_value = 5.0f;
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Event, {}, {},
            {.event_handler = "on_step", .event_value = &event_value}));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Stop, {}, {}));
    }

    TEST(ScriptInvocationTest, MethodsKeepPropertiesSeparateFromReadOnlyParameterSnapshots) {
        const auto script = Script::create(R"(
            local group = {
                properties = {speed = 1, direction = {1, 2, 3}},
                parameters = {speed = 999},
            }
            function group:read_speed() return self.parameters.speed end
            function group:on_start()
                self.properties.speed = 99
                self.config = self.parameters
                assert(self:read_speed() == 4)
            end
            function group:update(expected)
                assert(self:read_speed() == expected)
                assert(self.properties.speed == 99)
                assert((self.parameters == self.config) == (expected == 4))
            end
            function group:write_parameter() self.parameters.direction[1] = 0 end
            function group:on_stop() self:write_parameter() end
            return group
        )");
        ASSERT_TRUE(script) << script.error().message;
        const auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance) << instance.error().message;
        const auto first = script.value()->resolve_parameters({{"speed", 4.0f}});
        const auto changed = script.value()->resolve_parameters({{"speed", 7.0f}});
        ASSERT_TRUE(first);
        ASSERT_TRUE(changed);
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Start, {}, first.value()));
        for(int repeat = 0; repeat < 2; ++repeat)
            ASSERT_TRUE(instance.value()->invoke(
                Script::Phase::Update, {}, first.value(), {.delta_time = 4}));
        ASSERT_TRUE(instance.value()->invoke(
            Script::Phase::Update, {}, changed.value(), {.delta_time = 7}));
        const auto stopped = instance.value()->invoke(Script::Phase::Stop, {}, changed.value());
        ASSERT_FALSE(stopped);
        EXPECT_NE(stopped.error().message.find("read-only"), std::string::npos);
        EXPECT_EQ(std::get<float>(script.value()->properties().at("speed").default_value), 1);
        EXPECT_EQ(std::get<Math::Vec3>(script.value()->properties().at("direction").default_value),
            Math::Vec3(1, 2, 3));
    }

    TEST(ScriptInvocationTest, DefinitionMetamethodFieldsDoNotBecomeInstanceMetamethods) {
        const auto script = Script::create(R"(
            local function unexpected() error('definition metamethod was activated') end
            local group = {
                __index = unexpected, __newindex = unexpected, __pairs = unexpected,
                __len = unexpected, __gc = unexpected, __call = unexpected,
            }
            function group:check()
                assert(getmetatable == nil and setmetatable == nil and rawset == nil)
                assert(self.__gc == group.__gc)
                assert(self.missing == nil)
                self.value = 3
                assert(#self == 0)
                local count = 0
                for key in pairs(self) do
                    assert(key == 'parameters' or key == 'value')
                    count = count + 1
                end
                assert(count == 2)
            end
            function group:update() self:check() end
            return group
        )");
        ASSERT_TRUE(script) << script.error().message;
        const auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance) << instance.error().message;
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Update, {}, {}));
    }

    TEST(ScriptInvocationTest, MethodErrorsAndResourceLimitsStayInsideProtectedInvocation) {
        for(const auto& [body, message] : {
                std::pair{"error('helper failed')", "helper failed"},
                std::pair{"self:missing()", "missing"},
                std::pair{"while true do end", "instruction budget"},
                std::pair{"return self:run()", "instruction budget"},
                std::pair{"return string.rep('x', 16 * 1024 * 1024)", "memory"},
            }) {
            SCOPED_TRACE(body);
            const auto script =
                Script::create(std::string("local group = {}; function group:run() ") + body + R"(
                end
                function group:update() self:run() end
                function group:cleanup()
                    assert(debug == nil and getmetatable == nil and pcall == nil)
                    self.stopped = true
                end
                function group:on_stop() self:cleanup(); assert(self.stopped) end
                return group
            )",
                    "helper_error.lua");
            ASSERT_TRUE(script) << script.error().message;
            const auto instance = script.value()->instantiate();
            ASSERT_TRUE(instance) << instance.error().message;
            const auto result = instance.value()->invoke(Script::Phase::Update, {}, {});
            ASSERT_FALSE(result);
            EXPECT_NE(result.error().message.find("helper_error.lua"), std::string::npos);
            EXPECT_NE(result.error().message.find(message), std::string::npos);
            ASSERT_TRUE(instance.value()->invoke(Script::Phase::Stop, {}, {}));
        }
    }

    TEST(ScriptInvocationTest, NonStringErrorsUseReadableFallbackWithoutCallingUserTostring) {
        for(const auto* value : {"{}", "42", "false", "nil"}) {
            SCOPED_TRACE(value);
            const auto script = Script::create(
                std::string("tostring = function() error('user tostring called') end; ")
                    + "return {update = function() error(" + value + R"() end,
                    on_stop = function()
                        assert(debug == nil and getmetatable == nil and pcall == nil)
                    end}
                )",
                "nonstring.lua");
            ASSERT_TRUE(script) << script.error().message;
            const auto instance = script.value()->instantiate();
            ASSERT_TRUE(instance) << instance.error().message;
            const auto failed = instance.value()->invoke(Script::Phase::Update, {}, {});
            ASSERT_FALSE(failed);
            const auto& message = failed.error().message;
            EXPECT_TRUE(message.starts_with("@nonstring.lua: Lua raised a non-string error"));
            EXPECT_NE(message.find("stack traceback:"), std::string::npos);
            EXPECT_EQ(message.find("user tostring called"), std::string::npos);
            ASSERT_TRUE(instance.value()->invoke(Script::Phase::Stop, {}, {}));
        }
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

    TEST(ScriptSourceTest, EventDeclarationsRequireNamedMethodsAndBoundedValidNames) {
        for(const char* events : {"true", "'invalid'", "{[1] = 'on_event'}", "{[''] = 'on_event'}",
                R"({['test\0hidden'] = 'on_event'})", "{['test.event'] = ''}",
                R"({['test.event'] = 'on_event\0hidden'})", "{['test.event'] = false}",
                "{['test.event'] = function() end}", "{['test.event'] = 'missing'}",
                "{['test.event'] = 'not_a_function'}", "{[string.rep('x', 129)] = 'on_event'}"}) {
            SCOPED_TRACE(events);
            const auto code = std::string("return {on_event = function() end, not_a_function = 1, "
                                          "events = ")
                              + events + "}";
            const auto script = Script::create(code, "event_schema.lua");
            ASSERT_FALSE(script);
            EXPECT_NE(script.error().message.find("event_schema.lua"), std::string::npos);
        }
        for(const int count : {128, 129}) {
            const auto code = std::string("local script = {events = {}}; ")
                              + "function script:on_event(value) end; for i = 1, "
                              + std::to_string(count)
                              + " do script.events['test.' .. i] = 'on_event' end; return script";
            const auto script = Script::create(code);
            EXPECT_EQ(static_cast<bool>(script), count == 128);
        }
        EXPECT_TRUE(Script::create("return {events = {}}"));
    }

    TEST(ScriptInvocationTest, EmitValidatesRuntimeNamesAndSessionCompatiblePayloads) {
        Scene scene;
        const auto actor = scene.create_entity();
        const auto script =
            Script::create("return {update = function() comet.emit('test.event', true) end}");
        ASSERT_TRUE(script) << script.error().message;
        const auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        EXPECT_FALSE(instance.value()->invoke(Script::Phase::Update, actor, {}));
        EXPECT_FALSE(instance.value()->invoke(Script::Phase::Update, actor, {}, {.scene = &scene}));

        SceneRuntime runtime;
        ASSERT_TRUE(runtime.start(scene));
        for(const char* payload : {"nil", "false", "3.5", "'ready'", "{1, 2, 3}"}) {
            SCOPED_TRACE(payload);
            const auto emitting = Script::create(std::string("return {on_start = function() "
                                                             "comet.emit('test.event', ")
                                                 + payload + ") end}");
            ASSERT_TRUE(emitting) << emitting.error().message;
            const auto emitting_instance = emitting.value()->instantiate();
            ASSERT_TRUE(emitting_instance);
            const auto emitted = emitting_instance.value()->invoke(
                Script::Phase::Start, actor, {}, {.scene = &scene});
            EXPECT_TRUE(emitted) << emitted.error().message;
        }
        for(const char* invalid : {"comet.emit('', 1)", R"(comet.emit('test\0hidden', 1))",
                "comet.emit(7, 1)", "comet.emit('test.event', {1, 2, 3, 4})",
                "comet.emit('test.event', comet.self_entity())",
                "comet.emit('test.event', function() end)", "comet.emit('test.event', {1, '2', 3})",
                "comet.emit('test.event', math.huge)"}) {
            SCOPED_TRACE(invalid);
            const auto rejected =
                Script::create(std::string("return {update = function() ") + invalid + " end}");
            ASSERT_TRUE(rejected) << rejected.error().message;
            const auto rejected_instance = rejected.value()->instantiate();
            ASSERT_TRUE(rejected_instance);
            const auto emitted = rejected_instance.value()->invoke(
                Script::Phase::Update, actor, {}, {.scene = &scene});
            ASSERT_FALSE(emitted);
            EXPECT_FALSE(emitted.error().message.empty());
        }
        ASSERT_TRUE(runtime.stop());
    }

    TEST(ScriptSourceTest, InvalidPropertyConversionStaysWithinProtectedCall) {
        for(const char* invalid : {"{1, 'invalid', 3}", "{type = 'unknown'}", "function() end",
                "string.rep('x', 4097)"}) {
            const auto source =
                std::string("return {properties = {text = 'kept', invalid = ") + invalid + "}}";
            const auto script = Script::create(source, "invalid_properties.lua");
            ASSERT_FALSE(script);
            EXPECT_NE(script.error().message.find("invalid_properties.lua"), std::string::npos);
        }
        EXPECT_FALSE(Script::create("return {properties = {[''] = true}}"));
        EXPECT_FALSE(Script::create(R"(return {properties = {['a\0b'] = true}})"));
        EXPECT_TRUE(
            Script::create("return {properties = {text = 'valid', direction = {1, 2, 3}}}"));
        const auto overflow = Script::create(R"(
            local properties = {}
            for i = 1, 129 do properties['key' .. i] = string.rep('x', 64) end
            return {properties = properties}
        )");
        ASSERT_FALSE(overflow);
        EXPECT_NE(overflow.error().message.find("name/count"), std::string::npos);
    }

    TEST(ScriptInvocationTest, BindingMemoryFailureReturnsWithoutLosingHostSessionData) {
        Scene scene;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.start(scene));
        const std::string payload(4096, 'x');
        ASSERT_TRUE(runtime.get_session().set_value("payload", payload));
        const auto script = Script::create(R"(
            local script = {}
            function script:on_start()
                self.values = {}
                for i = 1, 4096 do self.values[i] = false end
            end
            function script:update()
                for i = 1, 4096 do self.values[i] = comet.session_get('payload') end
            end
            return script
        )");
        ASSERT_TRUE(script) << script.error().message;
        for(int attempt = 0; attempt < 3; ++attempt) {
            auto instance = script.value()->instantiate();
            ASSERT_TRUE(instance) << instance.error().message;
            ASSERT_TRUE(instance.value()->invoke(Script::Phase::Start, {}, {},
                {.scene = &scene, .session = &runtime.get_session()}));
            // 预留表容量后，8 MiB 上限命中 session_get 的 Lua 字符串分配。
            const auto result = instance.value()->invoke(Script::Phase::Update, {}, {},
                {.scene = &scene, .session = &runtime.get_session()});
            ASSERT_FALSE(result);
            EXPECT_NE(result.error().message.find("memory"), std::string::npos);
            ASSERT_TRUE(runtime.get_session().get_value("payload"));
            EXPECT_EQ(std::get<std::string>(*runtime.get_session().get_value("payload")), payload);
        }
        ASSERT_TRUE(runtime.stop());
    }

    TEST(ScriptInvocationTest, EntityReferencesDoNotCrossSceneGenerations) {
        Scene first;
        Scene second;
        const EntityUuid uuid = EntityUuid::generate();
        const auto original = first.create_entity_with_uuid(uuid);
        const auto replacement = second.create_entity_with_uuid(uuid);
        auto script = Script::create(R"(return {
            on_start = function(self)
                self.reference = comet.self_entity()
            end,
            update = function(self)
                assert(not self.reference:is_valid())
                assert(comet.find_entity(self.parameters.uuid):is_valid())
            end
        })");
        ASSERT_TRUE(script) << script.error().message;
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance) << instance.error().message;
        const ParameterMap parameters{{"uuid", uuid.to_string()}};
        ASSERT_TRUE(instance.value()->invoke(
            Script::Phase::Start, original, parameters, {.scene = &first}));
        ASSERT_TRUE(instance.value()->invoke(
            Script::Phase::Update, replacement, parameters, {.scene = &second}));
        EXPECT_FALSE(instance.value()->invoke(
            Script::Phase::Update, original, parameters, {.scene = &second}));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Stop, {}, parameters));
        ASSERT_TRUE(instance.value()->invoke(
            Script::Phase::Update, original, parameters, {.scene = &first}));
    }

    TEST(ScriptInvocationTest, EntityLookupReportsMalformedIdsAndMissingEntitiesSeparately) {
        Scene scene;
        const auto actor = scene.create_entity();
        auto script = Script::create(R"(return {
            update = function(self)
                assert(comet.find_entity(self.parameters.missing) == nil)
                comet.find_entity('not-a-uuid')
            end
        })");
        ASSERT_TRUE(script) << script.error().message;
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance) << instance.error().message;
        const ParameterMap parameters{{"missing", EntityUuid::generate().to_string()}};
        auto result =
            instance.value()->invoke(Script::Phase::Update, actor, parameters, {.scene = &scene});
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().message.find("Expected an entity UUID"), std::string::npos);
    }

    TEST(ScriptInvocationTest, EntityCreationOptionsPreserveEmptyRequestsAndSnapshotMeshHandles) {
        Scene scene;
        auto actor = scene.create_entity("Source");
        actor.add_component<MeshRendererComponent>(AssetHandle{11}, AssetHandle{12});
        TransformComponent source_transform;
        source_transform.translation = {9, 8, 7};
        source_transform.scale = {4, 4, 4};
        ASSERT_TRUE(actor.try_set_transform(source_transform));
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.start(scene));
        const auto script = Script::create(R"(return {
            properties = {offset = {1, 2, 3}},
            on_start = function(self)
                self.created = {
                    comet.create_entity(),
                    comet.create_entity('Named', {}),
                    comet.create_entity('NilOptions', nil),
                    comet.create_entity('Marker', {mesh_source = comet.self_entity()}),
                    comet.create_entity('Placed', {
                        translation = self.parameters.offset,
                        rotation = {10, 20, 30}, scale = {0.1, 0.2, 0.3},
                    }),
                }
                for i = 1, #self.created do
                    assert(type(self.created[i]) == 'string')
                    assert(comet.find_entity(self.created[i]) == nil)
                end
            end,
            update = function(self)
                for i = 1, #self.created do
                    assert(comet.find_entity(self.created[i]):is_valid())
                end
            end,
        })");
        ASSERT_TRUE(script) << script.error().message;
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        const auto parameters = script.value()->resolve_parameters({});
        ASSERT_TRUE(parameters);
        ASSERT_TRUE(instance.value()->invoke(
            Script::Phase::Start, actor, parameters.value(), {.scene = &scene}));
        EXPECT_EQ(scene.entity_count(), 1u);
        actor.get_component<MeshRendererComponent>().material = AssetHandle{13};
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_EQ(scene.entity_count(), 6u);
        ASSERT_TRUE(instance.value()->invoke(
            Script::Phase::Update, actor, parameters.value(), {.scene = &scene}));
        for(const auto created : scene.get_entities()) {
            if(created == actor)
                continue;
            const auto& name = created.get_component<NameComponent>().name;
            const auto& transform = created.get_component<TransformComponent>();
            if(name == "Placed") {
                EXPECT_EQ(transform.translation, Math::Vec3(1, 2, 3));
                EXPECT_EQ(transform.rotation, Math::Vec3(10, 20, 30));
                EXPECT_EQ(transform.scale, Math::Vec3(0.1f, 0.2f, 0.3f));
            } else {
                EXPECT_EQ(transform.translation, Math::Vec3(0));
                EXPECT_EQ(transform.rotation, Math::Vec3(0));
                EXPECT_EQ(transform.scale, Math::Vec3(1));
            }
            if(name == "Marker") {
                ASSERT_TRUE(created.has_component<MeshRendererComponent>());
                const auto& renderer = created.get_component<MeshRendererComponent>();
                EXPECT_EQ(renderer.mesh, AssetHandle{11});
                EXPECT_EQ(renderer.material, AssetHandle{12});
            } else
                EXPECT_FALSE(created.has_component<MeshRendererComponent>());
        }
        ASSERT_TRUE(runtime.stop());
    }

    void expect_rejected_creation_options(const std::string& options) {
        SCOPED_TRACE(options);
        Scene scene;
        auto actor = scene.create_entity();
        actor.add_component<MeshRendererComponent>(AssetHandle{11}, AssetHandle{12});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.start(scene));
        const auto script = Script::create(
            "return {properties = {vector = {1, 2, 3, 4}}, update = function(self) comet.create_entity('Invalid', "
            + options + ") end}");
        ASSERT_TRUE(script) << script.error().message;
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        const auto parameters = script.value()->resolve_parameters({});
        ASSERT_TRUE(parameters);
        EXPECT_FALSE(instance.value()->invoke(
            Script::Phase::Update, actor, parameters.value(), {.scene = &scene}));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(scene.entity_count(), 1u);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(ScriptInvocationTest, EntityCreationRejectsUnknownOptionsAndNonDenseFiniteVectors) {
        for(const char* options : {"false", "12", "'options'", "{unknown = 1}", "{[1] = 2}",
                "{mesh = 11, material = 12}", R"({['translation\0extra'] = {1, 2, 3}})",
                "{mesh_source = {}}", "{mesh_source = 'uuid'}", "{mesh_source = 11}",
                "{mesh_source = false}", "{mesh_source = comet.self_entity(), physics = true}",
                "{translation = self.parameters.vector}", "{translation = self.parameters}",
                "self.parameters", "self.parameters.vector"})
            expect_rejected_creation_options(options);
        for(const char* field : {"translation", "rotation", "scale"})
            for(const char* vector :
                {"true", "{}", "{1, 2}", "{1, 2, 3, 4}", "{[1]=1, [3]=3}", "{[0]=0, 1, 2, 3}",
                    "{1, 2, 3, extra=4}", "{[1.5]=1, [2]=2, [3]=3}", "{1, '2', 3}", "{1, true, 3}",
                    "{1, math.huge, 3}", "{1, 0/0, 3}", "{1, 1e100, 3}"})
                expect_rejected_creation_options(std::string("{") + field + " = " + vector + "}");
    }

    TEST(ScriptInvocationTest, EntityCreationRequiresAMeshSourceWithBothHandles) {
        Scene scene;
        auto actor = scene.create_entity();
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.start(scene));
        const auto script = Script::create(R"(return {
            update = function()
                comet.create_entity('Invalid', {mesh_source = comet.self_entity()})
            end,
        })");
        ASSERT_TRUE(script);
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        for(int configuration = 0; configuration < 3; ++configuration) {
            if(configuration == 1)
                actor.add_component<MeshRendererComponent>(AssetHandle{11}, AssetHandle{});
            if(configuration == 2)
                actor.get_component<MeshRendererComponent>() = {AssetHandle{}, AssetHandle{12}};
            const auto result =
                instance.value()->invoke(Script::Phase::Update, actor, {}, {.scene = &scene});
            ASSERT_FALSE(result);
            EXPECT_NE(result.error().message.find("Mesh source"), std::string::npos);
            ASSERT_TRUE(runtime.advance(0));
            EXPECT_EQ(scene.entity_count(), 1u);
        }
        ASSERT_TRUE(runtime.stop());
    }

    TEST(ScriptInvocationTest, EntityCreationRejectsReusedAndForeignMeshSourceReferences) {
        Scene first;
        auto original = first.create_entity();
        original.add_component<MeshRendererComponent>(AssetHandle{11}, AssetHandle{12});
        const auto uuid = original.get_uuid();
        SceneRuntime first_runtime;
        ASSERT_TRUE(first_runtime.start(first));
        const auto script = Script::create(R"(return {
            on_start = function(self) self.source = comet.self_entity() end,
            update = function(self)
                comet.create_entity('Invalid', {mesh_source = self.source})
            end,
        })");
        ASSERT_TRUE(script);
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        ASSERT_TRUE(
            instance.value()->invoke(Script::Phase::Start, original, {}, {.scene = &first}));
        first.destroy_entity(original);
        auto replacement = first.create_entity_with_uuid(uuid);
        replacement.add_component<MeshRendererComponent>(AssetHandle{11}, AssetHandle{12});
        auto result =
            instance.value()->invoke(Script::Phase::Update, replacement, {}, {.scene = &first});
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().message.find("stale"), std::string::npos);
        ASSERT_TRUE(first_runtime.advance(0));
        EXPECT_EQ(first.entity_count(), 1u);
        Scene second;
        auto foreign = second.create_entity_with_uuid(uuid);
        foreign.add_component<MeshRendererComponent>(AssetHandle{11}, AssetHandle{12});
        SceneRuntime second_runtime;
        ASSERT_TRUE(second_runtime.start(second));
        result = instance.value()->invoke(Script::Phase::Update, foreign, {}, {.scene = &second});
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().message.find("stale"), std::string::npos);
        ASSERT_TRUE(second_runtime.advance(0));
        EXPECT_EQ(second.entity_count(), 1u);
        ASSERT_TRUE(second_runtime.stop());
        ASSERT_TRUE(first_runtime.stop());
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
        const auto resolved = script.value()->resolve_parameters({});
        ASSERT_TRUE(resolved);
        const auto& parameters = resolved.value();
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Start, {}, parameters));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Update, {}, parameters));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Update, {}, parameters));
        auto stopped = instance.value()->invoke(Script::Phase::Stop, {}, parameters);
        ASSERT_FALSE(stopped);
        EXPECT_NE(stopped.error().message.find("read-only"), std::string::npos);
    }
}

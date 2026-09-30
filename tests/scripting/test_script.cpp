#include "scripting/script.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "input/input_actions.h"
#include "input/input_state.h"
#include <gtest/gtest.h>
#include <limits>

namespace Comet::Tests {
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
        const auto actor = scene.create_entity();
        InputState input;
        EXPECT_FALSE(instance.value()->invoke(
            Script::Phase::Update, actor, {}, {.scene = &scene, .input = &input}));
        EXPECT_FALSE(scene.take_restart_request());
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.start(scene));
        for(const auto phase : {Script::Phase::Start, Script::Phase::Stop}) {
            EXPECT_FALSE(instance.value()->invoke(phase, actor, {}, {.scene = &scene}));
            EXPECT_FALSE(scene.take_restart_request());
            EXPECT_FALSE(
                instance.value()->invoke(phase, actor, {}, {.scene = &scene, .input = &input}));
            EXPECT_FALSE(scene.take_restart_request());
        }
        for(const auto phase : {Script::Phase::Update, Script::Phase::FixedUpdate,
                Script::Phase::TriggerEnter, Script::Phase::TriggerExit,
                Script::Phase::CollisionEnter, Script::Phase::CollisionExit}) {
            const auto called = instance.value()->invoke(
                phase, actor, {}, {.scene = &scene, .contact_other = actor});
            ASSERT_TRUE(called) << called.error().message;
            EXPECT_TRUE(scene.take_restart_request());
            EXPECT_TRUE(scene.get_session_value("after.restart"));
        }
        EXPECT_FALSE(instance.value()->invoke(
            static_cast<Script::Phase>(-1), actor, {}, {.scene = &scene, .input = &input}));
        EXPECT_FALSE(scene.take_restart_request());
        scene.destroy_entity(actor);
        EXPECT_FALSE(instance.value()->invoke(
            Script::Phase::Update, actor, {}, {.scene = &scene, .input = &input}));
        EXPECT_FALSE(scene.take_restart_request());
        ASSERT_TRUE(runtime.stop());
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
        const auto actor = scene.create_entity();
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_input_actions(std::move(actions).value()));
        EXPECT_FALSE(instance.value()->invoke(Script::Phase::Start, actor, {}, {.scene = &scene}));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Start, actor, {}, {.scene = &scene}));
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
            EXPECT_FALSE(invalid_instance.value()->invoke(
                Script::Phase::Update, actor, {}, {.scene = &scene}));
        }
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Update, actor, {}, {.scene = &scene}));
        EXPECT_TRUE(scene.get_session_value("before.failure"));
        const auto failed = runtime.advance(0);
        ASSERT_FALSE(failed);
        EXPECT_NE(failed.error().message.find("missing"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_FALSE(scene.get_session_value("before.failure"));
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_TRUE(runtime.advance(0));
        ASSERT_TRUE(runtime.stop());
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
        ASSERT_TRUE(scene.set_session_value("value", Math::Vec3(1, 2, 3)));
        EXPECT_FALSE(scene.set_session_value("value", Math::Vec4(1, 2, 3, 4)));
        EXPECT_EQ(std::get<Math::Vec3>(*scene.get_session_value("value")), Math::Vec3(1, 2, 3));
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
        const auto result =
            instance.value()->invoke(Script::Phase::Update, {}, {}, {.scene = &scene});
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().message.find("three finite numbers"), std::string::npos);
        EXPECT_EQ(std::get<Math::Vec3>(*scene.get_session_value("value")), Math::Vec3(1, 2, 3));
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
            ASSERT_TRUE(scene.set_session_value("value", Math::Vec3(4, 5, 6)));
            const auto script = Script::create(
                std::string("return {update = function() comet.session_set('value', ") + vector
                + ") end}");
            ASSERT_TRUE(script) << script.error().message;
            auto instance = script.value()->instantiate();
            ASSERT_TRUE(instance);
            EXPECT_FALSE(
                instance.value()->invoke(Script::Phase::Update, {}, {}, {.scene = &scene}));
            EXPECT_EQ(std::get<Math::Vec3>(*scene.get_session_value("value")), Math::Vec3(4, 5, 6));
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
        ASSERT_TRUE(scene.set_session_value("payload", payload));
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
            ASSERT_TRUE(instance.value()->invoke(Script::Phase::Start, {}, {}, {.scene = &scene}));
            // 预留表容量后，8 MiB 上限命中 session_get 的 Lua 字符串分配。
            const auto result =
                instance.value()->invoke(Script::Phase::Update, {}, {}, {.scene = &scene});
            ASSERT_FALSE(result);
            EXPECT_NE(result.error().message.find("memory"), std::string::npos);
            ASSERT_TRUE(scene.get_session_value("payload"));
            EXPECT_EQ(std::get<std::string>(*scene.get_session_value("payload")), payload);
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

#include "reflection/type_registry.h"
#include "scene/settings_registration.h"
#include "scene/scene_settings.h"
#include "scene/component_registration.h"
#include "reflection/type_json.h"
#include "scene/component_registry.h"
#include "scene/prefab.h"
#include "scene/scene_serializer.h"
#include "scene/command_history.h"
#include <gtest/gtest.h>

namespace Comet::Tests {
    namespace {
        struct ReflectedTarget {
            EntityUuid entity;
            AssetHandle mesh;
            float weight = 1;
        };
        struct ReflectedSettings {
            std::vector<ReflectedTarget> targets{ReflectedTarget{}};
            std::optional<ReflectedTarget> fallback;
            std::array<float, 2> range{1, 2};
            int32_t priority = 0;
        };
        struct ReflectedComponent {
            ReflectedSettings settings;
        };

        TypeDescriptor target_schema() {
            return describe_type<ReflectedTarget>("target")
                .field(COMET_FIELD(ReflectedTarget, entity))
                .field(COMET_FIELD(ReflectedTarget, mesh), "网格", {.asset_type = AssetType::Mesh})
                .field(COMET_FIELD(ReflectedTarget, weight), "权重",
                    {.numeric = {.minimum = 0, .maximum = 10, .enforce_bounds = true}})
                .build();
        }
        TypeDescriptor settings_schema() {
            const auto target = target_schema();
            return describe_type<ReflectedSettings>("settings")
                .field(COMET_FIELD(ReflectedSettings, targets), "目标", target, {.max_elements = 2})
                .field(COMET_FIELD(ReflectedSettings, fallback), "备用", target)
                .field(COMET_FIELD(ReflectedSettings, range), "范围")
                .field(COMET_FIELD(ReflectedSettings, priority), "优先级")
                .build();
        }
        ComponentRegistry reflected_registry() {
            auto registry = create_scene_component_registry();
            EXPECT_TRUE(registry.register_component(
                describe_component<ReflectedComponent>("test_reflected", "复合属性")
                    .field(COMET_FIELD(ReflectedComponent, settings), "设置", settings_schema())
                    .build()));
            return registry;
        }

    }

    TEST(ReflectionTest, FluentSchemaResolvesNestedPathsAndKeepsSnapshotsIndependent) {
        const auto registry = reflected_registry();
        const auto& component = *registry.find_component("test_reflected");
        ASSERT_TRUE(component.valid());
        ReflectedComponent data;
        const auto before = component.find_property("settings")->copy_value(&data);
        auto weight = component.resolve_property("settings.targets[0].weight");
        ASSERT_TRUE(weight);
        ASSERT_TRUE(weight->assign_value(&data, 4.f));
        EXPECT_EQ(data.settings.targets[0].weight, 4);
        EXPECT_FALSE(weight->assign_value(&data, -1.f));
        EXPECT_EQ(data.settings.targets[0].weight, 4);
        EXPECT_NE(before, component.find_property("settings")->copy_value(&data));
        auto restored = ReflectedComponent{};
        ASSERT_TRUE(component.find_property("settings")->assign_value(&restored, *before));
        EXPECT_EQ(restored.settings.targets[0].weight, 1);
        EXPECT_FALSE(component.resolve_property("settings.targets[3].weight")->copy_value(&data));
        EXPECT_FALSE(component.resolve_property("settings.targets[-1]"));
        EXPECT_FALSE(component.resolve_property("settings..priority"));
        auto optional = component.resolve_property("settings.fallback.weight");
        ASSERT_TRUE(optional);
        EXPECT_FALSE(optional->copy_value(&data));
        data.settings.fallback.emplace();
        EXPECT_TRUE(optional->assign_value(&data, 5.f));
        EXPECT_EQ(data.settings.fallback->weight, 5);
    }

    TEST(ReflectionTest, JsonRejectsMalformedCollectionsAndPreservesDestinationOnFailure) {
        ReflectedSettings value;
        value.priority = -4;
        const auto schema = settings_schema();
        const Json::Context context("settings", "<test>");
        Json::Writer writer;
        ASSERT_TRUE(ReflectionJson::write_object(writer, schema, value, context, "settings"));
        const auto encoded = std::move(writer).finish();
        ASSERT_TRUE(encoded);
        ReflectedSettings restored;
        Json::Parser parser;
        auto node = context.parse(parser, encoded.value());
        ASSERT_TRUE(node);
        ASSERT_TRUE(
            ReflectionJson::read_object(schema, node.value(), restored, context, "settings"));
        EXPECT_EQ(restored.priority, -4);
        EXPECT_EQ(restored.range, value.range);
        EXPECT_FALSE(restored.fallback);
        for(const auto text :
            {R"({"targets":[],"fallback":null,"range":[1,2],"priority":1,"priority":2})",
                R"({"targets":[],"fallback":null,"range":[1],"priority":1})",
                R"({"targets":[],"fallback":{"entity":{"entity":"00000000-0000-0000-0000-000000000000"},"mesh":0,"weight":-1},"range":[1,2],"priority":1})",
                R"({"targets":[{},{},{}],"fallback":null,"range":[1,2],"priority":1})"}) {
            node = context.parse(parser, text);
            ASSERT_TRUE(node);
            EXPECT_FALSE(
                ReflectionJson::read_object(schema, node.value(), restored, context, "settings"));
            EXPECT_EQ(restored.priority, -4);
            EXPECT_EQ(restored.range, value.range);
        }
    }

    TEST(ReflectionTest, ScenePersistenceAndPrefabRemapUseTheSameNestedDescription) {
        const auto registry = reflected_registry();
        Scene scene;
        auto root = scene.create_entity("root");
        auto child = scene.create_entity("child");
        ASSERT_TRUE(scene.set_parent(child, root));
        ReflectedComponent component;
        component.settings.targets[0] = {child.get_uuid(), AssetHandle(73), 2};
        component.settings.fallback = ReflectedTarget{root.get_uuid(), AssetHandle(74), 3};
        component.settings.priority = -2;
        root.add_component<ReflectedComponent>(component);
        const SceneSerializer serializer(registry);
        const auto encoded = serializer.serialize(scene);
        ASSERT_TRUE(encoded) << encoded.error();
        const auto restored = serializer.deserialize(encoded.value());
        ASSERT_TRUE(restored) << restored.error();
        auto restored_root = restored.value()->find_entity(root.get_uuid());
        EXPECT_EQ(restored_root.get_component<ReflectedComponent>().settings.targets[0].entity,
            child.get_uuid());
        const auto prefab = Prefab::capture(scene, registry, root);
        ASSERT_TRUE(prefab) << prefab.error();
        Scene destination;
        const auto placed = prefab.value().instantiate(destination, registry);
        ASSERT_TRUE(placed) << placed.error();
        const auto& settings = placed.value().get_component<ReflectedComponent>().settings;
        EXPECT_NE(settings.targets[0].entity, child.get_uuid());
        EXPECT_TRUE(destination.find_entity(settings.targets[0].entity));
        ASSERT_TRUE(settings.fallback);
        EXPECT_EQ(settings.fallback->entity, placed.value().get_uuid());
        const auto references = registry.collect_asset_references(destination);
        EXPECT_TRUE(std::ranges::any_of(
            references, [](const auto& ref) { return ref.handle == AssetHandle(73); }));
        EXPECT_TRUE(std::ranges::any_of(
            references, [](const auto& ref) { return ref.handle == AssetHandle(74); }));
    }

    TEST(ReflectionTest, NestedPropertyUndoRestoresOnlyTheEditedField) {
        const auto registry = reflected_registry();
        Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<ReflectedComponent>();
        CometEditor::CommandHistory history;
        history.bind_scene(&scene);
        CometEditor::PropertyEditTransaction edit(history, registry);
        const CometEditor::PropertyEditTransaction::Target target{
            entity.get_uuid(), "test_reflected", "settings.targets[0].weight"};
        ASSERT_TRUE(edit.apply(target, 7.f));
        EXPECT_EQ(entity.get_component<ReflectedComponent>().settings.targets[0].weight, 7);
        entity.get_component<ReflectedComponent>().settings.priority = -9;
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(entity.get_component<ReflectedComponent>().settings.targets[0].weight, 1);
        EXPECT_EQ(entity.get_component<ReflectedComponent>().settings.priority, -9);
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(entity.get_component<ReflectedComponent>().settings.targets[0].weight, 7);
    }
    TEST(ReflectionTest, RuntimeDiscoveryAndConstructionShareRegistrationsAcrossEngineBoundary) {
        const auto schema = settings_schema(); // 可执行文件注册，由 engine 动态库查询。
        const auto found = Reflection::find_type("settings");
        ASSERT_TRUE(found);
        EXPECT_EQ(found->properties.size(), schema.properties.size());
        const auto names = Reflection::registered_types();
        EXPECT_NE(std::ranges::find(names, "settings"), names.end());
        auto created = Reflection::create("settings");
        ASSERT_TRUE(created) << created.error();
        auto& object = created.value();
        ASSERT_TRUE(object.assign_property("targets[0].weight", 4.f));
        EXPECT_EQ(object.copy_property("targets[0].weight"), std::optional<PropertyValue>(4.f));
        EXPECT_FALSE(object.assign_property("targets[0].weight", -1.f));
        EXPECT_FALSE(object.assign_property("targets[0].weight", true));
        EXPECT_EQ(object.copy_property("targets[0].weight"), std::optional<PropertyValue>(4.f));
        EXPECT_FALSE(Reflection::find_type("not_registered"));
        EXPECT_FALSE(Reflection::create("not_registered"));
        auto another = Reflection::create("settings");
        ASSERT_TRUE(another);
        EXPECT_EQ(
            another.value().copy_property("targets[0].weight"), std::optional<PropertyValue>(1.f));
    }

    TEST(ReflectionTest, RegisteredMethodsUseNativeInstancesAndRejectWrongArgumentTypes) {
        struct Counter {
            enum class Mode { First, Second };
            float value = 1;
            Mode mode = Mode::First;
            void change(Mode value) { mode = value; }
            Mode current() const { return mode; }
            float add(float amount) {
                value += amount;
                return value;
            }
            void reset() { value = 1; }
        };
        const auto schema = describe_type<Counter>("counter")
                                .field(COMET_FIELD(Counter, value))
                                .enum_field(COMET_FIELD(Counter, mode), "模式",
                                    {{Counter::Mode::First, {"first", "First"}},
                                        {Counter::Mode::Second, {"second", "Second"}}})
                                .method<&Counter::change>("change")
                                .method<&Counter::current>("current")
                                .method<&Counter::add>("add")
                                .method<&Counter::reset>("reset")
                                .build();
        auto created = Reflection::create(schema.id);
        ASSERT_TRUE(created) << created.error();
        const std::array<PropertyValue, 1> increment{2.f};
        auto result = created.value().invoke("add", increment);
        ASSERT_TRUE(result) << result.error();
        EXPECT_EQ(result.value(), std::optional<PropertyValue>(3.f));
        EXPECT_EQ(created.value().copy_property("value"), std::optional<PropertyValue>(3.f));
        EXPECT_FALSE(created.value().invoke("add"));
        const std::array<PropertyValue, 1> wrong{std::string("2")};
        EXPECT_FALSE(created.value().invoke("add", wrong));
        EXPECT_FALSE(created.value().invoke("absent"));
        EXPECT_EQ(created.value().copy_property("value"), std::optional<PropertyValue>(3.f));
        result = created.value().invoke("reset");
        ASSERT_TRUE(result) << result.error();
        EXPECT_FALSE(result.value());
        EXPECT_EQ(created.value().copy_property("value"), std::optional<PropertyValue>(1.f));

        const std::array<PropertyValue, 1> mode{std::string("second")};
        ASSERT_TRUE(created.value().invoke("change", mode));
        auto selected = created.value().invoke("current");
        ASSERT_TRUE(selected);
        EXPECT_EQ(selected.value(), std::optional<PropertyValue>(std::string("second")));
        const std::array<PropertyValue, 1> invalid_mode{std::string("missing")};
        EXPECT_FALSE(created.value().invoke("change", invalid_mode));
        EXPECT_EQ(created.value().copy_property("mode"),
            std::optional<PropertyValue>(std::string("second")));

        (void)post_process_descriptor();
        auto effects = Reflection::create("post_process");
        ASSERT_TRUE(effects) << effects.error();
        const auto methods = Reflection::methods("post_process");
        EXPECT_NE(std::ranges::find(methods, "uses_bloom"), methods.end());
        ASSERT_TRUE(effects.value().assign_property("bloom_enabled", true));
        auto active = effects.value().invoke("uses_bloom");
        ASSERT_TRUE(active) << active.error();
        EXPECT_EQ(active.value(), std::optional<PropertyValue>(true));
    }

    TEST(ReflectionTest, SequentialViewsPreserveBooleanProxyValuesAndFixedArraySize) {
        struct Flags {
            std::vector<bool> enabled{true, false};
            std::array<float, 2> weights{1, 2};
        };
        const auto schema = describe_type<Flags>("flags")
                                .field(COMET_FIELD(Flags, enabled))
                                .field(COMET_FIELD(Flags, weights))
                                .build();
        auto object = Reflection::create(schema.id);
        ASSERT_TRUE(object) << object.error();
        EXPECT_EQ(object.value().copy_property("enabled[0]"), std::optional<PropertyValue>(true));
        EXPECT_EQ(object.value().copy_property("enabled[1]"), std::optional<PropertyValue>(false));
        ASSERT_TRUE(object.value().assign_property("enabled[1]", true));
        EXPECT_EQ(object.value().copy_property("enabled[1]"), std::optional<PropertyValue>(true));
        const auto before = object.value().copy_property("weights");
        PropertyCollection::Data shortened{
            .kind = PropertyCollection::Kind::Sequence, .elements = {3.f}};
        EXPECT_FALSE(
            object.value().assign_property("weights", PropertyCollection(std::move(shortened))));
        EXPECT_EQ(object.value().copy_property("weights"), before);
    }

}

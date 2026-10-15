#include "asset/data/game_data.h"
#include "asset/project_prepare.h"
#include "asset/product_index.h"
#include "core/project.h"
#include "scene/component_registry.h"
#include "scene/prefab.h"
#include "scene/scene_serializer.h"
#include "scene/script_component.h"
#include "scripting/script_instance.h"
#include "support/asset_manager_fixture.h"

namespace Comet::Tests {
    namespace {
        constexpr auto schema = R"({"fields":{
            "name":{"type":"string"},"hp":{"type":"integer","min":1,"max":200},
            "speed":{"type":"number","min":0},"enabled":{"type":"bool"},"rules":{"type":"json"}
        }})";
        constexpr auto csv =
            "id,name,hp,speed,enabled,rules\r\n"
            "hero,\"角色,\"\"一\"\"\n说明\",100,2.75,true,\"{\"\"hits\"\":[1,2]}\"\r\n";
        constexpr auto simple =
            R"({"fields":{"hp":{"type":"integer","min":1}},"rows":[{"id":"hero","hp":100}]})";

        void write_asset(const ProjectPaths& paths, std::string_view relative, AssetHandle handle,
            AssetType type, std::string_view text) {
            const auto path = paths.assets() / relative;
            ASSERT_TRUE(write_text_file_atomic(path, text));
            ASSERT_TRUE(
                MetadataSerializer{}.save({.handle = handle, .type = type}, metadata_path(path)));
        }
    }

    TEST(GameDataTest, CsvQuotedUnicodeAndNestedJsonRoundTripWithoutTypeOrPrecisionLoss) {
        auto data = GameData::parse_csv(std::string("\xef\xbb\xbf") + csv, schema);
        ASSERT_TRUE(data) << data.error();
        const auto* hero = data.value().find("hero");
        ASSERT_NE(hero, nullptr);
        EXPECT_EQ(std::get<std::string>(hero->at("name").value), "角色,\"一\"\n说明");
        EXPECT_EQ(std::get<int64_t>(hero->at("hp").value), 100);
        EXPECT_DOUBLE_EQ(std::get<double>(hero->at("speed").value), 2.75);
        EXPECT_TRUE(std::get<bool>(hero->at("enabled").value));
        const auto encoded = data.value().serialize();
        ASSERT_TRUE(encoded);
        auto restored = GameData::parse_json(encoded.value());
        ASSERT_TRUE(restored) << restored.error();
        EXPECT_EQ(restored.value().serialize().value(), encoded.value());
        EXPECT_EQ(restored.value().find("missing"), nullptr);
    }

    TEST(GameDataTest, RejectsBadIdsFieldsRangesReferencesAndMalformedCsv) {
        for(const auto* text : {R"({"fields":{},"rows":[{"id":"a"},{"id":"a"}]})",
                R"({"fields":{},"rows":[{"id":""}]})", R"({"fields":{},"rows":[{"id":1}]})",
                R"({"fields":{},"rows":[{"id":"a","extra":1}]})",
                R"({"fields":{"hp":{"type":"integer"}},"rows":[{"id":"a","hp":1.5}]})",
                R"({"fields":{"hp":{"type":"integer","min":1}},"rows":[{"id":"a","hp":0}]})",
                R"({"fields":{"hp":{"type":"number","max":2}},"rows":[{"id":"a","hp":3}]})",
                R"({"fields":{"hp":{"type":"number","min":2,"max":1}},"rows":[]})",
                R"({"fields":{"hp":{"type":"string","min":1}},"rows":[]})",
                R"({"fields":{"hp":{"type":"integer"}},"rows":[{"id":"a"}]})",
                R"({"fields":{"hp":{"type":"bool"}},"rows":[{"id":"a","hp":"true"}]})",
                R"({"fields":{"hp":{"type":"asset","asset_type":"prefab"}},"rows":[{"id":"a","hp":"01"}]})",
                R"({"fields":{"hp":{"type":"asset"}},"rows":[]})",
                R"({"fields":{"rules":{"type":"json"}},"rows":[{"id":"a","rules":{"x":1,"x":2}}]})",
                R"({"fields":{"rules":{"type":"json"}},"rows":[{"id":"a","rules":[null]}]})",
                R"({"fields":{"hp":{"type":"integer"}},"rows":[{"id":"a","hp":9007199254740992}]})"}) {
            SCOPED_TRACE(text);
            EXPECT_FALSE(GameData::parse_json(text));
        }
        for(const auto* text : {"", "id,id\nx,x", "id,name,hp,speed,enabled,rules\na,x,1",
                "id,name,hp,speed,enabled,rules\na,\"bad",
                "id,name,hp,speed,enabled,rules\na,\"bad\"x,1,1,true,{}",
                "id,name,hp,speed,enabled,rules\na,x,0,1,true,{}",
                "id,name,hp,speed,enabled,rules\na,x,1,NaN,true,{}"})
            EXPECT_FALSE(GameData::parse_csv(text, schema)) << text;
        auto exact = GameData::parse_json(
            R"({"fields":{"target":{"type":"asset","asset_type":"data"}},"rows":[{"id":"x","target":"18446744073709551610"}]})");
        ASSERT_TRUE(exact) << exact.error();
        ASSERT_EQ(exact.value().references().size(), 1u);
        EXPECT_EQ(exact.value().references().front().handle.value(), 18446744073709551610ull);
    }

    TEST(GameDataTest, LuaReadsIndependentCopiesAndReportsUnavailableAssets) {
        const AssetHandle handle{18446744073709551610ull};
        auto data = GameData::parse_csv(csv, schema);
        ASSERT_TRUE(data);
        AssetRegistry registry;
        ASSERT_TRUE(
            registry.register_asset(handle, std::make_shared<GameData>(std::move(data).value())));
        auto script = Script::create(R"(
            return {properties={stats={type='data'}}, update=function(self)
                local ids = comet.data_ids(self.parameters.stats)
                assert(#ids == 1 and ids[1] == 'hero')
                assert(comet.data_get(self.parameters.stats, 'missing') == nil)
                local row = comet.data_get(self.parameters.stats, 'hero')
                assert(row.hp == 100 and row.speed == 2.75 and row.enabled)
                assert(row.rules.hits[2] == 2)
                row.hp = 0; row.rules.hits[2] = 99
                local fresh = comet.data_get(self.parameters.stats, 'hero')
                assert(fresh.hp == 100 and fresh.rules.hits[2] == 2)
            end}
        )");
        ASSERT_TRUE(script) << script.error().message;
        ParameterMap parameters{{"stats", std::to_string(handle.value())}};
        auto references = script.value()->asset_references(parameters);
        ASSERT_TRUE(references);
        ASSERT_EQ(references.value().size(), 1u);
        EXPECT_EQ(references.value().front().type, AssetType::Data);
        auto instance = ScriptInstance::create(*script.value());
        ASSERT_TRUE(instance);
        const auto invoke = [&] {
            return instance.value()->invoke(
                ScriptInstance::Phase::Update, {}, parameters, {.assets = &registry});
        };
        ASSERT_TRUE(invoke());
        ASSERT_TRUE(invoke());
        ASSERT_TRUE(registry.unregister_asset(handle));
        EXPECT_FALSE(invoke());
    }

    TEST(GameDataTest, SchemaEditsInvalidateCachedDataAndBrokenReferencesAreDiagnosed) {
        TemporaryProject project;
        write_asset(
            project.paths(), "stats.csv", AssetHandle(11), AssetType::Data, "id,hp\nhero,100\n");
        ASSERT_TRUE(write_text_file_atomic(project.paths().assets() / "stats.csv.schema.json",
            R"({"fields":{"hp":{"type":"integer","min":1}}})"));
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetDatabase database(project.paths());
        AssetManager manager(database, registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto first = manager.load_data(AssetHandle(11));
        ASSERT_TRUE(first) << first.error().message;
        const auto revision = manager.get_database().get_revision(AssetHandle(11));
        ASSERT_TRUE(write_text_file_atomic(project.paths().assets() / "stats.csv.schema.json",
            R"({"fields":{"hp":{"type":"integer","min":150}}})"));
        const std::array<std::filesystem::path, 1> schema_change{"stats.csv.schema.json"};
        const std::array<std::filesystem::path, 1> data_change{"stats.csv"};
        EXPECT_FALSE(database.scan_changed_sources(schema_change));
        EXPECT_FALSE(database.scan_changed_sources(data_change));
        EXPECT_FALSE(manager.scan().succeeded());
        EXPECT_NE(manager.get_database().get_revision(AssetHandle(11)), revision);
        EXPECT_FALSE(registry.contains(AssetHandle(11)));
        EXPECT_FALSE(manager.load_data(AssetHandle(11)));
        EXPECT_NE(first.value()->find("hero"), nullptr);
        write_asset(
            project.paths(), "stats.csv", AssetHandle(11), AssetType::Data, "id,hp\nhero,160\n");
        EXPECT_TRUE(manager.scan().succeeded());
        EXPECT_TRUE(manager.load_data(AssetHandle(11)));
        write_asset(project.paths(), "bad.data.json", AssetHandle(12), AssetType::Data,
            R"({"fields":{"unit":{"type":"asset","asset_type":"prefab"}},"rows":[{"id":"x","unit":"11"}]})");
        EXPECT_FALSE(manager.scan().succeeded());
    }

    TEST(GameDataTest, ExportIncludesTransitiveDataAndPrefabDependenciesWithoutCsvOrSourceFiles) {
        TemporaryProject source;
        TemporaryDirectory outputs;
        const auto components = create_scene_component_registry();
        write_asset(source.paths(), "stats.csv", AssetHandle(11), AssetType::Data,
            "id,hp,rules\nhero,100,12\n");
        ASSERT_TRUE(write_text_file_atomic(source.paths().assets() / "stats.csv.schema.json",
            R"({"fields":{"hp":{"type":"integer"},"rules":{"type":"asset","asset_type":"data"}}})"));
        write_asset(source.paths(), "rules.data.json", AssetHandle(12), AssetType::Data,
            R"({"fields":{"unit":{"type":"asset","asset_type":"prefab"},"stats":{"type":"asset","asset_type":"data"}},"rows":[{"id":"hero","unit":"13","stats":"11"}]})");
        Scene tree;
        auto root = tree.create_entity("Unit");
        auto prefab = Prefab::capture(tree, components, root);
        ASSERT_TRUE(prefab);
        ASSERT_TRUE(prefab.value().save(source.paths().assets() / "unit.prefab", components));
        ASSERT_TRUE(
            MetadataSerializer{}.save({.handle = AssetHandle(13), .type = AssetType::Prefab},
                source.paths().assets() / "unit.prefab.meta"));
        write_asset(source.paths(), "actor.lua", AssetHandle(14), AssetType::Script,
            "return {properties={stats={type='data'}}}");
        Scene scene;
        auto& binding = scene.create_entity("Actor").add_component<ScriptComponent>();
        binding.asset = AssetHandle(14);
        binding.parameters = {{"stats", std::string("11")}};
        ASSERT_TRUE(
            SceneSerializer(components).save(scene, source.paths().assets() / "main.scene"));
        ASSERT_TRUE(write_text_file_atomic(source.paths().root() / "project.json",
            R"({"version":2,"id":"00000000-0000-4000-8000-000000000001","name":"Data test","startup_scene":"main.scene","input_contexts":[],"input_actions":[]})"));
        auto project = Project::load(source.paths().root());
        ASSERT_TRUE(project) << project.error();
        auto exported = export_project_assets(project.value(), outputs.path() / "game", {});
        ASSERT_TRUE(exported) << exported.error();
        std::filesystem::remove_all(source.paths().root());
        ProjectPaths paths(outputs.path() / "game");
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "stats.csv"));
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "stats.csv.schema.json"));
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(paths, registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        auto restored = SceneSerializer(components).load(paths.assets() / "main.scene");
        ASSERT_TRUE(restored);
        auto references = manager.prepare_scene(*restored.value(), components);
        ASSERT_TRUE(references) << references.error().message;
        EXPECT_TRUE(registry.contains<Prefab>(AssetHandle(13)));
        const auto data = registry.resolve<GameData>(AssetHandle(11));
        ASSERT_TRUE(data);
        EXPECT_EQ(std::get<int64_t>(data->find("hero")->at("hp").value), 100);
        EXPECT_TRUE(registry.contains<GameData>(AssetHandle(12)));
        EXPECT_FALSE(std::filesystem::exists(paths.cache()));
    }
}

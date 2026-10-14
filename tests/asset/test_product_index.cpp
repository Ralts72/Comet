#include "support/asset_manager_fixture.h"
#include "asset/default_assets.h"
#include "asset/serialization/animation_controller_serializer.h"
#include "asset/import/import_service.h"
#include "asset/product_index.h"
#include "asset/project_prepare.h"
#include "core/project.h"
#include "scene/component_registry.h"
#include "scene/prefab.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "scene/script_component.h"
#include "scene/scene_runtime.h"
#include "scene/systems/script_system.h"

namespace Comet::Tests {
    TEST(ProductIndexTest, RoundTripPreservesIdentityDependenciesAndSamplerWithoutSourceMetadata) {
        const TemporaryProject project;
        const AssetHandle texture{11}, material{22};
        const SamplerDesc sampler{.min_filter = Filter::Nearest,
            .address_mode_u = SamplerAddressMode::ClampToEdge,
            .address_mode_v = SamplerAddressMode::MirrorRepeat,
            .max_anisotropy = 4};
        ProductIndex index;
        index.assets = {{texture, AssetType::Texture, "products/11.texture", {}, sampler},
            {material, AssetType::Material, "materials/22.mat", {texture}}};
        const auto path = project.paths().root() / ProductIndex::FILENAME;
        ASSERT_TRUE(index.save(path));
        auto restored = ProductIndex::load(path);
        ASSERT_TRUE(restored) << restored.error();
        ASSERT_EQ(restored.value().assets.size(), 2);
        EXPECT_EQ(restored.value().assets.front().sampler, sampler);

        AssetDatabase database(project.paths());
        const auto scan = database.scan();
        ASSERT_TRUE(scan.succeeded());
        EXPECT_TRUE(database.is_read_only());
        ASSERT_TRUE(database.find(texture));
        EXPECT_EQ(database.find(texture)->path, "products/11.texture");
        EXPECT_FALSE(database.find(texture)->source_asset);
        EXPECT_EQ(database.find(std::filesystem::path("materials/22.mat"))->handle, material);
        EXPECT_EQ(database.get_dependents(texture).size(), 1);
        EXPECT_EQ(database.get_dependents(texture).front(), material);
        const auto revision = database.get_revision(material);
        EXPECT_FALSE(database.scan().snapshot_updated);
        const std::array<std::filesystem::path, 1> changed{"materials/22.mat"};
        ASSERT_TRUE(database.scan_changed_sources(changed));
        EXPECT_EQ(database.get_revision(material), revision);
        EXPECT_FALSE(database.update_dependencies(material, {}));
        EXPECT_FALSE(database.update_import_dependencies(material, {"source.gltf"}));
        EXPECT_FALSE(database.update_import_settings(texture, TextureImportSettings{}));
        EXPECT_FALSE(database.open_product_index(index));
        EXPECT_TRUE(std::filesystem::is_empty(project.paths().assets()));
        EXPECT_FALSE(std::filesystem::exists(project.paths().cache()));
    }

    TEST(ProductIndexTest, RejectsAmbiguousIdentitySourceTypesEscapingPathsAndMissingDependencies) {
        ProductIndex index;
        index.assets = {{AssetHandle{11}, AssetType::Mesh, "products/11.mesh"}};
        ASSERT_TRUE(index.validate());
        for(const auto* path : {"", "/tmp/mesh", "../mesh", "products/../mesh", "C:/mesh",
                "products\\mesh", "./mesh"}) {
            auto invalid = index;
            invalid.assets.front().path = path;
            EXPECT_FALSE(invalid.validate()) << path;
        }
        auto invalid = index;
        invalid.assets.push_back(index.assets.front());
        EXPECT_FALSE(invalid.validate());
        invalid.assets.back().handle = AssetHandle{22};
        EXPECT_FALSE(invalid.validate());
        invalid = index;
        invalid.assets.front().dependencies = {AssetHandle{22}};
        EXPECT_FALSE(invalid.validate());
        invalid.assets.front().dependencies = {DEFAULT_CHARACTER_MESH};
        EXPECT_FALSE(invalid.validate());
        invalid.assets.front().dependencies = {DEFAULT_PBR_MATERIAL};
        EXPECT_TRUE(invalid.validate());
        for(const auto type : {AssetType::Unknown, AssetType::Model, AssetType::Shader}) {
            invalid = index;
            invalid.assets.front().type = type;
            EXPECT_FALSE(invalid.validate());
        }
        invalid = index;
        invalid.assets.front().handle = DEFAULT_PBR_MATERIAL;
        invalid.assets.front().type = AssetType::Material;
        EXPECT_FALSE(invalid.validate());
        invalid = index;
        invalid.assets.front().sampler = SamplerDesc{};
        EXPECT_FALSE(invalid.validate());
    }

    TEST(ProductIndexTest, InvalidIndexNeverFallsBackToSourceScanningOrCreatesSidecars) {
        const TemporaryProject project;
        ASSERT_TRUE(write_text_file_atomic(project.paths().assets() / "actor.lua", "return {}"));
        const auto path = project.paths().root() / ProductIndex::FILENAME;
        for(const auto* invalid : {"{", R"({"version":2,"assets":[]})",
                R"({"version":1,"assets":[{"handle":1,"type":"mesh","path":"../bad","dependencies":[]}]})"}) {
            ASSERT_TRUE(write_text_file_atomic(path, invalid));
            AssetDatabase database(project.paths());
            EXPECT_FALSE(database.scan().succeeded());
            EXPECT_TRUE(database.get_assets().empty());
            EXPECT_FALSE(std::filesystem::exists(project.paths().assets() / "actor.lua.meta"));
        }
        const TemporaryProject outside;
        std::filesystem::create_directory_symlink(
            outside.paths().assets(), project.paths().assets() / "escape");
        ProductIndex index;
        index.assets = {{AssetHandle{1}, AssetType::Mesh, "escape/mesh"}};
        ASSERT_TRUE(index.save(path));
        AssetDatabase database(project.paths());
        EXPECT_FALSE(database.scan().succeeded());
        EXPECT_TRUE(database.get_assets().empty());
    }

    TEST(ProductIndexTest, LoadsCookedAssetsAndLuaModulesAfterDeletingSourcesAndImportCache) {
        const TemporaryProject source, product;
        const AssetHandle mesh{11}, texture{22}, material{33}, script{44};
        source.add_external_mesh(mesh);
        source.add_texture(texture);
        AssetDatabase source_database(source.paths());
        ASSERT_TRUE(source_database.scan().succeeded());
        ImportService imports(source.paths());
        auto geometry = imports.build_mesh_artifact(mesh, "meshes/external.gltf", 64 * 1024 * 1024);
        ASSERT_TRUE(geometry) << geometry.error();
        ASSERT_TRUE(geometry.value().publish_atomic(product.paths().assets() / "mesh.bin"));
        // 内置身份也必须使用导出的产品；用独特的三角形验证未回退到内置 GLB。
        geometry.value().handle = DEFAULT_CHARACTER_MESH;
        ASSERT_TRUE(geometry.value().publish_atomic(product.paths().assets() / "character.bin"));
        ASSERT_TRUE(imports.prepare_texture(
            *source_database.find(texture), TextureImportSettings{}, 64 * 1024 * 1024));
        std::filesystem::copy_file(
            imports.texture_artifact_path(texture), product.paths().assets() / "texture.bin");
        ASSERT_TRUE(MaterialSerializer{}.save(
            {.template_name = "pbr", .texture_properties = {{"base_color_texture", texture}}},
            product.paths().assets() / "surface.mat"));
        std::filesystem::create_directories(product.paths().assets() / "scripts");
        ASSERT_TRUE(write_text_file_atomic(
            product.paths().assets() / "scripts/shared.module.lua", "return {step = 7}"));
        ASSERT_TRUE(write_text_file_atomic(product.paths().assets() / "scripts/actor.lua", R"(
            local shared = require('scripts.shared')
            return {update = function() comet.translate(shared.step, 0, 0) end}
        )"));
        const SamplerDesc sampler{.address_mode_u = SamplerAddressMode::ClampToEdge};
        ProductIndex index;
        index.assets = {{mesh, AssetType::Mesh, "mesh.bin"},
            {DEFAULT_CHARACTER_MESH, AssetType::Mesh, "character.bin"},
            {texture, AssetType::Texture, "texture.bin", {}, sampler},
            {material, AssetType::Material, "surface.mat", {texture}},
            {script, AssetType::Script, "scripts/actor.lua"}};
        ASSERT_TRUE(index.save(product.paths().root() / ProductIndex::FILENAME));
        std::filesystem::remove_all(source.paths().root());
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(product.paths(), registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        auto cpu_mesh = manager.read_mesh_data(mesh);
        ASSERT_TRUE(cpu_mesh) << cpu_mesh.error().message;
        EXPECT_EQ(cpu_mesh.value().vertices.size(), 3);
        ASSERT_TRUE(manager.load_mesh(mesh));
        ASSERT_TRUE(manager.load_mesh(DEFAULT_CHARACTER_MESH));
        EXPECT_EQ(factory.last_mesh_vertex_count(), 3);
        auto loaded_material = manager.load_material(material);
        ASSERT_TRUE(loaded_material) << loaded_material.error().message;
        EXPECT_TRUE(registry.contains<Texture>(texture));
        EXPECT_EQ(factory.last_texture_sampler(), sampler);
        auto loaded_script = manager.load_script(script);
        ASSERT_TRUE(loaded_script) << loaded_script.error().message;
        Scene scene;
        auto actor = scene.create_entity();
        actor.add_component<ScriptComponent>().asset = script;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(registry)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(actor.get_component<TransformComponent>().translation, Math::Vec3(7, 0, 0));
        EXPECT_FALSE(manager.import_mesh(mesh));
        EXPECT_FALSE(manager.import_mesh_async(mesh));
        EXPECT_FALSE(manager.reimport_texture(texture, {}));
        EXPECT_FALSE(manager.prepare_material_update(material, {.template_name = "pbr"}));
        EXPECT_FALSE(std::filesystem::exists(product.paths().cache()));
        for(const auto& entry :
            std::filesystem::recursive_directory_iterator(product.paths().assets()))
            EXPECT_NE(entry.path().extension(), ".meta");

        AssetRegistry empty_registry;
        AssetManager reopened(product.paths(), empty_registry, factory, scheduler);
        ASSERT_TRUE(reopened.scan().succeeded());
        std::filesystem::remove(product.paths().assets() / "character.bin");
        EXPECT_FALSE(reopened.load_mesh(DEFAULT_CHARACTER_MESH));
        ASSERT_TRUE(write_text_file_atomic(product.paths().assets() / "texture.bin", "corrupt"));
        EXPECT_FALSE(reopened.load_texture(texture));
        EXPECT_FALSE(empty_registry.contains(texture));
    }

    TEST(ProductIndexTest, ExportsBuiltinAndProjectControllersWithoutSourceFallback) {
        const TemporaryProject source;
        const TemporaryDirectory outputs;
        const auto components = create_scene_component_registry();
        const AssetHandle custom{42};
        AnimationController definition;
        definition.parameters = {{"open", false}};
        const auto custom_path = source.paths().assets() / "door.ani";
        ASSERT_TRUE(AnimationControllerSerializer{}.save(definition, custom_path));
        ASSERT_TRUE(
            MetadataSerializer{}.save({.handle = custom, .type = AssetType::AnimationController},
                metadata_path(custom_path)));
        Scene scene;
        scene.create_entity("Door").add_component<AnimatorComponent>(
            AnimatorComponent{.controller = custom});
        scene.create_entity("Character")
            .add_component<AnimatorComponent>(
                AnimatorComponent{.controller = DEFAULT_CHARACTER_ANIMATION});
        ASSERT_TRUE(
            SceneSerializer(components).save(scene, source.paths().assets() / "main.scene"));
        ASSERT_TRUE(write_text_file_atomic(source.paths().root() / "project.json", R"({
            "version":2,"id":"00000000-0000-4000-8000-000000000001",
            "name":"Controllers","startup_scene":"main.scene"})"));
        const auto project = Project::load(source.paths().root());
        ASSERT_TRUE(project) << project.error();
        const auto exported = outputs.path() / "game";
        const auto result = export_project_assets(project.value(), exported, {});
        ASSERT_TRUE(result) << result.error();
        std::filesystem::remove_all(source.paths().root());
        const ProjectPaths paths(exported);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(paths, registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.get_database().is_read_only());
        const auto loaded = manager.load_animation_controller(custom);
        ASSERT_TRUE(loaded) << loaded.error().message;
        EXPECT_EQ(*loaded.value(), definition);
        auto builtin = manager.load_animation_controller(DEFAULT_CHARACTER_ANIMATION);
        ASSERT_TRUE(builtin) << builtin.error().message;
        EXPECT_TRUE(builtin.value()->parameters.contains("grounded"));
        const auto product = manager.get_database().find(DEFAULT_CHARACTER_ANIMATION);
        ASSERT_NE(product, nullptr);
        ASSERT_TRUE(
            AnimationControllerSerializer{}.save(definition, paths.assets() / product->path));
        AssetRegistry other_registry;
        AssetManager reopened(paths, other_registry, factory, scheduler);
        ASSERT_TRUE(reopened.scan().succeeded());
        builtin = reopened.load_animation_controller(DEFAULT_CHARACTER_ANIMATION);
        ASSERT_TRUE(builtin) << builtin.error().message;
        EXPECT_EQ(*builtin.value(), definition);
        std::filesystem::remove(paths.assets() / product->path);
        ASSERT_TRUE(other_registry.unregister_asset(DEFAULT_CHARACTER_ANIMATION));
        EXPECT_FALSE(reopened.load_animation_controller(DEFAULT_CHARACTER_ANIMATION));
        EXPECT_FALSE(std::filesystem::exists(paths.cache()));
    }

    TEST(ProductIndexTest, ExportedLuaPrefabSpawnsUsePreparedAssetsAfterSourceDeletion) {
        const TemporaryProject source;
        const TemporaryDirectory outputs;
        const auto components = create_scene_component_registry();
        const AssetHandle mesh{11}, child_script{44}, spawner_script{55};
        const AssetHandle prefab_handle{18446744073709551610ull};
        source.add_external_mesh(mesh);
        const auto write_script = [&](AssetHandle handle, const char* path, const char* code) {
            const auto file = source.paths().assets() / path;
            EXPECT_TRUE(write_text_file_atomic(file, code));
            EXPECT_TRUE(MetadataSerializer{}.save(
                {.handle = handle, .type = AssetType::Script}, metadata_path(file)));
        };
        write_script(child_script, "child.lua", R"(
            return {properties={owner={type='entity'}}, on_start=function(self)
                assert(self.parameters.owner:is_valid())
                comet.session_set('child.started', true)
            end}
        )");
        write_script(spawner_script, "spawn.lua", R"(
            return {properties={unit={type='prefab'}}, on_start=function(self)
                local first = comet.instantiate_prefab(self.parameters.unit, {2,3,4})
                local second = comet.instantiate_prefab(self.parameters.unit)
                assert(first ~= second)
                assert(comet.find_entity(first) == nil)
                comet.session_set('spawn.first', first)
                comet.session_set('spawn.second', second)
            end}
        )");
        Scene tree;
        auto root = tree.create_entity("Unit");
        root.add_component<MeshRendererComponent>(
            MeshRendererComponent{.mesh = mesh, .material = DEFAULT_UNLIT_MATERIAL});
        auto child = tree.create_entity("Sensor");
        ASSERT_TRUE(tree.set_parent(child, root));
        auto& child_binding = child.add_component<ScriptComponent>();
        child_binding.asset = child_script;
        child_binding.parameters = {{"owner", root.get_uuid()}};
        auto prefab = Prefab::capture(tree, components, root);
        ASSERT_TRUE(prefab);
        const auto prefab_path = source.paths().assets() / "unit.prefab";
        ASSERT_TRUE(prefab.value().save(prefab_path, components));
        ASSERT_TRUE(MetadataSerializer{}.save(
            {.handle = prefab_handle, .type = AssetType::Prefab}, metadata_path(prefab_path)));
        Scene scene;
        auto& spawn = scene.create_entity("Spawner").add_component<ScriptComponent>();
        spawn.asset = spawner_script;
        spawn.parameters = {{"unit", std::to_string(prefab_handle.value())}};
        ASSERT_TRUE(
            SceneSerializer(components).save(scene, source.paths().assets() / "startup.scene"));
        ASSERT_TRUE(write_text_file_atomic(source.paths().root() / "project.json", R"({
            "version":2,"id":"00000000-0000-4000-8000-000000000001",
            "name":"Prefab runtime","startup_scene":"startup.scene",
            "input_contexts":[],"input_actions":[]})"));
        const auto project = Project::load(source.paths().root());
        ASSERT_TRUE(project) << project.error();
        ASSERT_TRUE(prepare_project(project.value()));
        const auto exported = outputs.path() / "game";
        const auto result = export_project_assets(project.value(), exported, {});
        ASSERT_TRUE(result) << result.error();
        std::filesystem::remove_all(source.paths().root());
        const ProjectPaths paths(exported);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(paths, registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.get_database().find(prefab_handle));
        ASSERT_TRUE(manager.get_database().find(mesh));
        ASSERT_TRUE(manager.get_database().find(child_script));
        auto loaded = SceneSerializer(components).load(paths.assets() / "startup.scene");
        ASSERT_TRUE(loaded) << loaded.error();
        auto references = manager.collect_scene_references(*loaded.value(), components);
        ASSERT_TRUE(references) << references.error().message;
        ASSERT_TRUE(manager.prepare_references(
            references.value(), AssetManager::MissingAssetPolicy::FailRequired));
        ASSERT_TRUE(manager
                .references_ready(
                    references.value(), AssetManager::MissingAssetPolicy::FailRequired)
                .value());
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(registry)));
        const auto started = runtime.start(*loaded.value());
        ASSERT_TRUE(started) << started.error().message;
        EXPECT_EQ(loaded.value()->entity_count(), 5);
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(runtime.get_session().get_value("child.started"));
        const auto first_value = runtime.get_session().get_value("spawn.first");
        ASSERT_TRUE(first_value);
        const auto id = EntityUuid::parse(std::get<std::string>(*first_value));
        ASSERT_TRUE(id);
        const auto first = loaded.value()->find_entity(*id);
        ASSERT_TRUE(first);
        EXPECT_EQ(first.get_component<TransformComponent>().translation, Math::Vec3(2, 3, 4));
        EXPECT_EQ(first.get_component<MeshRendererComponent>().mesh, mesh);
        EXPECT_FALSE(first.has_component<PrefabInstanceComponent>());
        EXPECT_FALSE(std::filesystem::exists(paths.cache()));
        ASSERT_TRUE(runtime.stop());
    }

    TEST(ProductIndexTest, ExportsBothDemoLevelsAndLoadsThemAfterMovingProductsAndDeletingSource) {
        const TemporaryDirectory directory;
        const ProjectPaths source(directory.path() / "source");
        const std::filesystem::path demo = COMET_SAMPLE_PROJECT_DIRECTORY;
        std::filesystem::create_directories(source.root());
        std::filesystem::copy(
            demo / "assets", source.assets(), std::filesystem::copy_options::recursive);
        std::filesystem::copy_file(demo / "project.json", source.root() / "project.json");
        auto project = Project::load(source.root());
        ASSERT_TRUE(project) << project.error();
        const auto original_id = project.value().id();
        const auto target = directory.path() / "exported";
        const std::array<std::filesystem::path, 1> scenes{"scenes/character_second.scene"};
        const auto exported = export_project_assets(project.value(), target, scenes);
        ASSERT_TRUE(exported) << exported.error();
        const auto components = create_scene_component_registry();
        const SceneSerializer serializer(components);
        std::map<std::filesystem::path, std::size_t> entity_counts;
        for(const auto& relative : {project.value().startup_scene(), scenes.front()}) {
            const auto original = serializer.load((source.assets() / relative).string());
            ASSERT_TRUE(original);
            entity_counts[relative] = original.value()->get_entities().size();
        }
        const auto manifest = read_text_file(target / ProductIndex::FILENAME);
        ASSERT_TRUE(manifest);
        EXPECT_FALSE(export_project_assets(project.value(), target, scenes));
        EXPECT_EQ(read_text_file(target / ProductIndex::FILENAME).value(), manifest.value());
        const auto broken = directory.path() / "broken";
        const std::array<std::filesystem::path, 1> missing{"scenes/not_present.scene"};
        EXPECT_FALSE(export_project_assets(project.value(), broken, missing));
        EXPECT_FALSE(std::filesystem::exists(broken));
        for(const auto& entry : std::filesystem::directory_iterator(directory.path()))
            EXPECT_FALSE(entry.path().filename().string().starts_with(".comet-export-"));

        const ProjectPaths moved(directory.path() / "moved");
        std::filesystem::rename(target, moved.root());
        std::filesystem::remove_all(source.root());
        const auto reopened = Project::load(moved.root());
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().id(), original_id);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(moved, registry, factory, scheduler);
        const auto scan = manager.scan();
        for(const auto& issue : scan.issues)
            ADD_FAILURE() << issue.message;
        ASSERT_TRUE(scan.succeeded());
        ASSERT_TRUE(manager.get_database().is_read_only());
        for(const auto& [relative, count] : entity_counts) {
            const auto scene = serializer.load((moved.assets() / relative).string());
            ASSERT_TRUE(scene) << scene.error();
            EXPECT_EQ(scene.value()->get_entities().size(), count);
            for(auto entity : scene.value()->get_entities())
                EXPECT_FALSE(entity.has_component<PrefabInstanceComponent>());
            auto collected = manager.prepare_scene(*scene.value(), components);
            ASSERT_TRUE(collected) << collected.error().message;
            const auto references = std::move(collected).value();
            EXPECT_TRUE(registry.contains<Prefab>(AssetHandle{7863069821640201103ULL}));
            scheduler.wait_idle();
            ASSERT_TRUE(manager.process_completions());
            const auto ready = manager.references_ready(
                references, AssetManager::MissingAssetPolicy::FailRequired);
            ASSERT_TRUE(ready) << ready.error().message;
            EXPECT_TRUE(ready.value());
        }
        auto character = manager.read_mesh_data(DEFAULT_CHARACTER_MESH);
        auto music = manager.load_audio(AssetHandle{13857136248767905807ULL});
        ASSERT_TRUE(music) << music.error().message;
        EXPECT_TRUE(music.value()->is_streamed());
        EXPECT_TRUE(music.value()->samples().empty());
        EXPECT_EQ(music.value()->source_path().parent_path(), moved.assets() / "audio");
        ASSERT_TRUE(character) << character.error().message;
        EXPECT_TRUE(character.value().animation);
        EXPECT_TRUE(std::filesystem::exists(moved.assets() / "ui/runtime.rml"));
        EXPECT_TRUE(std::filesystem::exists(moved.assets() / "ui/runtime.ui.lua"));
        EXPECT_TRUE(
            std::filesystem::exists(moved.assets() / "scripts/character_progress.module.lua"));
        EXPECT_FALSE(std::filesystem::exists(moved.local_data()));
        for(const auto& entry : std::filesystem::recursive_directory_iterator(moved.assets())) {
            const auto extension = entry.path().extension();
            EXPECT_NE(extension, ".meta");
            EXPECT_NE(extension, ".gltf");
            EXPECT_NE(extension, ".glb");
        }
    }

    TEST(ProductIndexTest, EnvironmentProductUsesAsyncBudgetAndPublishesWithoutHdrOrCache) {
        const TemporaryProject source, product;
        const AssetHandle handle{51};
        const AssetRecord record{
            .handle = handle, .type = AssetType::Environment, .path = "sky.hdr"};
        write_hdr(source.paths().assets() / record.path, 8, 4);
        auto prepared = ImportService(source.paths())
                            .prepare_environment(record, EnvironmentImporter::MAX_WORKING_BYTES);
        ASSERT_TRUE(prepared) << prepared.error();
        const auto path = product.paths().assets() / "sky.bin";
        ASSERT_TRUE(prepared.value().publish_atomic(path));
        ProductIndex index;
        index.assets = {{handle, AssetType::Environment, "sky.bin"}};
        ASSERT_TRUE(index.save(product.paths().root() / ProductIndex::FILENAME));
        std::filesystem::remove_all(source.paths().root());
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(product.paths(), registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        BlockedWorker blocked(scheduler);
        ASSERT_TRUE(manager.request_load(handle, AssetType::Environment));
        EXPECT_FALSE(registry.contains(handle));
        blocked.release();
        scheduler.wait_idle();
        const auto published = completed_handles(manager.process_completions());
        ASSERT_EQ(published, std::vector{handle});
        EXPECT_TRUE(registry.contains<Environment>(handle));
        EXPECT_EQ(factory.texture_creation_count(), 4);
        EXPECT_FALSE(std::filesystem::exists(product.paths().cache()));

        AssetRegistry limited_registry;
        AssetImportLimits limits;
        limits.async.working_bytes = std::filesystem::file_size(path) - 1;
        AssetManager limited(product.paths(), limited_registry, factory, scheduler, limits);
        ASSERT_TRUE(limited.scan().succeeded());
        EXPECT_FALSE(limited.request_load(handle, AssetType::Environment));
        EXPECT_FALSE(limited_registry.contains(handle));
    }
}

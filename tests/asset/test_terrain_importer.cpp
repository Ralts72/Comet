#include "asset/import/terrain_importer.h"
#include "asset/import/texture_importer.h"
#include "asset/import/import_service.h"
#include "asset/import/image_encoding.h"
#include "asset/data/terrain_data.h"
#include "asset/artifact/model_artifact.h"
#include "asset/serialization/metadata_serializer.h"
#include "asset/database.h"
#include "asset/project_prepare.h"
#include "core/project.h"
#include "scene/scene.h"
#include "scene/components.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"
#include "gameplay/wind_sway.h"
#include "scene/scene_runtime.h"
#include "scene/systems/physics_system.h"
#include "physics/physics_service.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"
#include "asset/asset_manager.h"
#include "asset/registry.h"
#include "core/task_scheduler.h"
#include "support/render_resource_factory.h"
#include <gtest/gtest.h>
#include <cmath>

namespace Comet::Tests {
    namespace {
        TerrainData terrain() {
            TerrainData data;
            data.resolution = 9;
            data.chunk_cells = 4;
            data.albedo_resolution = 32;
            data.width = data.depth = 8;
            data.height_scale = 2;
            data.vegetation.count = 20;
            data.vegetation.wind_amplitude = 7;
            data.vegetation.max_slope = 70;
            data.heights.resize(81);
            for(uint32_t z = 0; z < 9; ++z)
                for(uint32_t x = 0; x < 9; ++x)
                    data.heights[z * 9 + x] = x / 10.f;
            return data;
        }
    }
    TEST(TerrainImporterTest, ValidatesAuthoringAndPreservesRawPngAlpha) {
        auto data = terrain();
        auto json = data.serialize();
        ASSERT_TRUE(json) << json.error();
        auto decoded = TerrainData::deserialize(json.value());
        ASSERT_TRUE(decoded) << decoded.error();
        EXPECT_EQ(decoded.value(), data);
        data.resolution = 10;
        EXPECT_FALSE(data.validate());
        data = terrain();
        data.heightmap = "height.png";
        EXPECT_FALSE(data.validate());
        data = terrain();
        data.layers[0].tiling = 0;
        EXPECT_FALSE(data.validate());
        data = terrain();
        data.vegetation.count = 10001;
        EXPECT_FALSE(data.validate());
        EXPECT_FALSE(TerrainData::deserialize(R"({"resolution":9,"resolution":17})"));
        TextureData rgba{.width = 1, .height = 1, .pixels = {7, 23, 201, 19}};
        auto encoded = encode_png_rgba(rgba);
        ASSERT_TRUE(encoded);
        auto texture = TextureImporter{}.import(std::as_bytes(std::span(encoded.value())), {});
        ASSERT_TRUE(texture) << texture.error();
        EXPECT_EQ(texture.value().pixels, rgba.pixels);
        rgba.pixels.pop_back();
        EXPECT_FALSE(encode_png_rgba(rgba));
    }
    TEST(
        TerrainImporterTest, ChunksShareEdgesAndNormalsWhileLodsReduceTrianglesAndScatterIsStable) {
        TemporaryDirectory temp;
        const auto path = temp.path() / "field.terrain";
        ASSERT_TRUE(terrain().save(path));
        const auto estimate = ModelImporter::working_bytes(path);
        ASSERT_TRUE(estimate) << estimate.error();
        EXPECT_FALSE(ModelImporter{}.import(path, estimate.value() - 1));
        auto imported = ModelImporter{}.import(path, estimate.value());
        ASSERT_TRUE(imported) << imported.error();
        const auto& model = imported.value();
        ASSERT_EQ(model.meshes.size(), 13u);
        ASSERT_EQ(model.materials.size(), 2u);
        EXPECT_EQ(model.meshes[0].lods, (std::array<uint32_t, 2>{1, 2}));
        EXPECT_TRUE(model.meshes[0].collision);
        EXPECT_LT(model.meshes[1].data.indices.size(), model.meshes[0].data.indices.size());
        EXPECT_LT(model.meshes[2].data.indices.size(), model.meshes[1].data.indices.size());
        for(uint32_t z = 0; z < 5; ++z) {
            const auto& a = model.meshes[0].data.vertices[z * 5 + 4];
            const auto& b = model.meshes[3].data.vertices[z * 5];
            EXPECT_EQ(a.position + model.nodes[0].transform.translation,
                b.position + model.nodes[1].transform.translation);
            EXPECT_EQ(a.normal, b.normal);
            EXPECT_EQ(a.texcoord, b.texcoord);
        }
        EXPECT_LT(model.meshes[0].data.vertices.back().position.y,
            model.meshes[0].data.vertices.front().position.y);
        EXPECT_GT(model.nodes.size(), 4u);
        for(size_t i = 4; i < model.nodes.size(); ++i)
            EXPECT_EQ(model.nodes[i].meshes, (std::vector<uint32_t>{12}));
        auto again = ModelImporter{}.import(path);
        ASSERT_TRUE(again);
        ASSERT_EQ(again.value().nodes.size(), model.nodes.size());
        for(size_t i = 4; i < model.nodes.size(); ++i)
            EXPECT_EQ(
                again.value().nodes[i].transform.translation, model.nodes[i].transform.translation);
        const AssetMetadata metadata{.handle = AssetHandle{32857}, .type = AssetType::Model};
        auto bound = ModelImporter::bind(imported.value(), metadata);
        ASSERT_TRUE(bound) << bound.error();
        auto rebound = ModelImporter::bind(std::move(again).value(), bound.value().metadata);
        ASSERT_TRUE(rebound);
        EXPECT_EQ(bound.value().metadata, rebound.value().metadata);
        Scene scene;
        auto root = scene.instantiate_model(bound.value().model, "terrain");
        ASSERT_TRUE(root) << root.error();
        EXPECT_EQ(scene.component_count<MeshLodComponent>(), 4u);
        EXPECT_EQ(scene.component_count<WindSwayComponent>(), 20u);
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::WindSwaySystem>()));
        ASSERT_TRUE(runtime.start(scene));
        const auto nodes = scene.get_entities();
        auto grass = *std::ranges::find_if(
            nodes, [](Entity e) { return e.has_component<WindSwayComponent>(); });
        const auto baseline = grass.get_component<TransformComponent>().rotation;
        ASSERT_TRUE(runtime.advance(.1));
        const auto swayed = grass.get_component<TransformComponent>().rotation;
        EXPECT_NE(swayed, baseline);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(.2));
        EXPECT_EQ(grass.get_component<TransformComponent>().rotation, swayed);
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(grass.get_component<TransformComponent>().rotation, baseline);
        EXPECT_EQ(scene.component_count<ColliderComponent>(), 4u);
        EXPECT_EQ(scene.component_count<RigidBodyComponent>(), 4u);
        scene.each<ColliderComponent>([](Entity, const auto& collider) {
            EXPECT_EQ(collider.shape, ColliderShape::TriangleMesh);
        });
    }
    TEST(TerrainImporterTest, HeightAndLayerDependenciesProducePortableModelProducts) {
        TemporaryDirectory temp;
        const ProjectPaths paths(temp.path());
        std::filesystem::create_directories(paths.assets());
        auto data = terrain();
        data.heights.clear();
        data.heightmap = "height.png";
        data.layers[0].texture = "layer.png";
        data.layers[0].normal_texture = "normal.png";
        data.layers[0].roughness_texture = "layer.png";
        TextureData pixels{.width = 9, .height = 9};
        pixels.pixels.assign(9 * 9 * 4, 128);
        auto png = encode_png_rgba(pixels);
        ASSERT_TRUE(png);
        ASSERT_TRUE(write_text_file_atomic(paths.assets() / "height.png", png.value()));
        ASSERT_TRUE(write_text_file_atomic(paths.assets() / "layer.png", png.value()));
        TextureData normal{.width = 1, .height = 1, .pixels = {180, 128, 245, 255}};
        auto normal_png = encode_png_rgba(normal);
        ASSERT_TRUE(normal_png);
        ASSERT_TRUE(write_text_file_atomic(paths.assets() / "normal.png", normal_png.value()));
        const auto source = paths.assets() / "ground.terrain";
        ASSERT_TRUE(data.save(source));
        const AssetMetadata metadata{.handle = AssetHandle{58971}, .type = AssetType::Model};
        ASSERT_TRUE(MetadataSerializer{}.save(metadata, metadata_path(source)));
        AssetDatabase database(paths);
        ASSERT_TRUE(database.scan().succeeded());
        const ImportService imports(paths);
        auto prepared = imports.prepare_model(
            *database.find(metadata.handle), AssetImportLimits{}.mesh_working_bytes);
        ASSERT_TRUE(prepared) << prepared.error();
        const auto index = imports.model_artifact_path(metadata.handle);
        auto artifact = ModelArtifact::load(index, metadata.handle);
        ASSERT_TRUE(artifact);
        ASSERT_EQ(artifact->model.nodes.size(), 24u);
        const auto& part = artifact->model.nodes[0].parts[0];
        EXPECT_TRUE(part.lod.level1);
        EXPECT_TRUE(part.lod.level2);
        EXPECT_TRUE(part.collision);
        EXPECT_EQ(artifact->source_inputs.files.size(), 4u);
        EXPECT_TRUE(std::filesystem::exists(artifact->product_path(index, part.lod.level2)));
        Scene scene;
        ASSERT_TRUE(scene.instantiate_model(artifact->model, "Terrain"));
        const auto components = create_scene_component_registry();
        ASSERT_TRUE(
            SceneSerializer(components).save(scene, (paths.assets() / "startup.scene").string()));
        ASSERT_TRUE(write_text_file_atomic(paths.root() / "project.json", R"({
            "version":2,"id":"00000000-0000-4000-8000-000000000002",
            "name":"Terrain product","startup_scene":"startup.scene",
            "input_contexts":[],"input_actions":[]})"));
        auto project = Project::load(paths.root());
        ASSERT_TRUE(project);
        const ProjectPaths products(temp.path() / "exported");
        auto exported = export_project_assets(project.value(), products.root());
        ASSERT_TRUE(exported) << exported.error();
        std::filesystem::remove(source);
        std::filesystem::remove_all(paths.cache());
        std::filesystem::remove(paths.assets() / "height.png");
        std::filesystem::remove(paths.assets() / "layer.png");
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(products, registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        EXPECT_TRUE(manager.get_database().is_read_only());
        EXPECT_TRUE(manager.load_mesh(part.mesh));
        EXPECT_TRUE(manager.load_mesh(part.lod.level1));
        EXPECT_TRUE(manager.load_mesh(part.lod.level2));
        EXPECT_TRUE(manager.load_material(part.material));
        for(const auto& record : manager.get_database().get_assets()) {
            EXPECT_NE(record.path.extension(), ".terrain");
            if(record.type == AssetType::Texture)
                EXPECT_TRUE(manager.load_texture(record.handle));
        }
        // 输入尺寸不匹配必须在导入时失败，而不是发布缺块模型。
        ASSERT_TRUE(write_text_file_atomic(paths.assets() / "height.png", png.value()));
        ASSERT_TRUE(write_text_file_atomic(paths.assets() / "layer.png", png.value()));
        data.resolution = 17;
        data.chunk_cells = 4;
        ASSERT_TRUE(data.save(source));
        EXPECT_FALSE(ModelImporter{}.import(source));
    }
    TEST(TerrainImporterTest, CustomVegetationPreservesMaterialsAndSharesImportedGeometry) {
        TemporaryDirectory temp;
        const auto source = temp.path() / "field.terrain";
        std::filesystem::copy_file(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/models/milk_truck.glb",
            temp.path() / "plant.glb");
        auto data = terrain();
        data.vegetation.model = "plant.glb";
        data.vegetation.count = 3;
        ASSERT_TRUE(data.save(source));
        auto info = TerrainImporter::inspect_source(source);
        ASSERT_TRUE(info) << info.error();
        EXPECT_NE(std::ranges::find(info.value().dependencies, temp.path() / "plant.glb"),
            info.value().dependencies.end());
        auto candidate = ModelImporter{}.import(source);
        ASSERT_TRUE(candidate) << candidate.error();
        const auto& model = candidate.value();
        ASSERT_EQ(model.nodes.size(), 7u);
        ASSERT_GT(model.meshes.size(), 13u);
        ASSERT_GT(model.materials.size(), 2u);
        EXPECT_EQ(model.nodes[4].meshes, model.nodes[5].meshes);
        EXPECT_EQ(model.nodes[5].meshes, model.nodes[6].meshes);
        EXPECT_EQ(model.nodes[4].wind.x, 7);
        for(auto index : model.nodes[4].meshes) {
            EXPECT_GE(index, 12u);
            EXPECT_GE(model.meshes[index].material, 1u);
            EXPECT_LT(model.meshes[index].material, model.materials.size());
        }
        const auto parts = model.nodes[4].meshes.size();
        auto bound = ModelImporter::bind(std::move(candidate).value(),
            AssetMetadata{.handle = AssetHandle{31841}, .type = AssetType::Model});
        ASSERT_TRUE(bound) << bound.error();
        EXPECT_EQ(bound.value().model.nodes[4].parts.size(), parts);
    }

    TEST(TerrainImporterTest, HierarchicalStaticChunksSupportFallingBodiesAndFollowRootPlacement) {
        TemporaryDirectory temp;
        auto data = terrain();
        data.vegetation.count = 0;
        std::ranges::fill(data.heights, .25f);
        const auto path = temp.path() / "floor.terrain";
        ASSERT_TRUE(data.save(path));
        auto imported = ModelImporter{}.import(path);
        ASSERT_TRUE(imported) << imported.error();
        auto bound = ModelImporter::bind(std::move(imported).value(),
            AssetMetadata{.handle = AssetHandle{38151}, .type = AssetType::Model});
        ASSERT_TRUE(bound) << bound.error();
        Scene scene;
        auto placed = scene.instantiate_model(bound.value().model, "terrain");
        ASSERT_TRUE(placed);
        auto root = placed.value();
        root.edit_transform([](auto& t) {
            t.translation = {2, 1, 3};
            t.rotation.y = 30;
            t.scale = Math::Vec3(1.5f);
        });
        Entity chunk;
        scene.each<const RigidBodyComponent>([&](Entity e, const auto&) {
            if(!chunk)
                chunk = e;
        });
        ASSERT_TRUE(chunk);
        const auto local = chunk.get_component<TransformComponent>();
        auto box = scene.create_entity("falling box");
        box.edit_transform([](auto& t) { t.translation = {2, 6, 3}; });
        box.add_component<RigidBodyComponent>();
        box.add_component<ColliderComponent>();
        PhysicsService physics({.read = [&](AssetHandle handle) -> Result<MeshData, Error> {
            for(const auto& item : bound.value().meshes)
                if(item.handle == handle)
                    return Result<MeshData, Error>::success(item.data);
            return Result<MeshData, Error>::failure({"Missing terrain mesh"});
        }});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        auto started = runtime.start(scene);
        ASSERT_TRUE(started) << started.error().message;
        for(int i = 0; i < 180; ++i) {
            auto step = runtime.advance(1.0 / 60);
            ASSERT_TRUE(step) << step.error().message;
        }
        EXPECT_NEAR(box.get_component<TransformComponent>().translation.y, 2.25f, .03f);
        EXPECT_EQ(chunk.get_component<TransformComponent>().translation, local.translation);
        EXPECT_EQ(chunk.get_component<TransformComponent>().scale, local.scale);
        root.edit_transform([](auto& t) { t.translation.y += 2; });
        ASSERT_TRUE(runtime.advance(1.0 / 60));
        auto hit = physics.raycast({2, 10, 3}, {0, -20, 0}, {.ignored = box.get_uuid()});
        ASSERT_TRUE(hit) << hit.error();
        ASSERT_TRUE(hit.value());
        EXPECT_NEAR(hit.value()->position.y, 3.75f, .001f);
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(physics.get_statistics().bodies, 0u);
    }

}

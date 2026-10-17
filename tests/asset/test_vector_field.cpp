#include "asset/data/vector_field_data.h"
#include "asset/asset_manager.h"
#include "asset/registry.h"
#include "asset/project_prepare.h"
#include "asset/serialization/metadata_serializer.h"
#include "common/file_io.h"
#include "core/project.h"
#include "core/task_scheduler.h"
#include "scene/scene.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"
#include "support/render_resource_factory.h"
#include "support/temporary_directory.h"
#include "render/resource/vector_field.h"
#include <gtest/gtest.h>

namespace Comet::Tests {
    TEST(VectorFieldTest, TrilinearSamplingRoundTripsAndRejectsMalformedGrids) {
        VectorFieldData field;
        for(int z = 0; z < 2; ++z)
            for(int y = 0; y < 2; ++y)
                for(int x = 0; x < 2; ++x)
                    field.values.emplace_back(x * 2.f - 1, y * 2.f - 1, z * 2.f - 1);
        EXPECT_LT(glm::length(field.sample({.3f, -.4f, .7f}) - Math::Vec3(.3f, -.4f, .7f)), .00001);
        EXPECT_EQ(field.sample({1, 1, 1}), Math::Vec3(1));
        EXPECT_EQ(field.sample({1.01f, 0, 0}), Math::Vec3(0));
        auto text = field.serialize();
        ASSERT_TRUE(text);
        auto loaded = VectorFieldData::deserialize(text.value());
        ASSERT_TRUE(loaded);
        EXPECT_EQ(loaded.value().values, field.values);
        field.dimensions.x = 33;
        EXPECT_FALSE(field.serialize());
        field.dimensions.x = 2;
        field.values[0].x = NAN;
        EXPECT_FALSE(field.serialize());
        EXPECT_FALSE(VectorFieldData::deserialize(
            R"({"dimensions":[2.5,2,2],"minimum":[0,0,0],"maximum":[1,1,1],"values":[]})"));
    }
    TEST(VectorFieldTest, AssetReferenceExportsAndLoadsWithoutTheSourceProject) {
        TemporaryDirectory directory, destination;
        const ProjectPaths paths(directory.path()), products(destination.path() / "game");
        const auto file = paths.assets() / "wind.vfield";
        VectorFieldData field;
        field.values.resize(8, {2, 0, 1});
        ASSERT_TRUE(write_text_file_atomic(file, field.serialize().value()));
        constexpr AssetHandle handle(80402);
        ASSERT_TRUE(MetadataSerializer{}.save(
            {.handle = handle, .type = AssetType::VectorField}, metadata_path(file)));
        FakeRenderResourceFactory factory;
        AssetRegistry registry;
        TaskScheduler tasks(1);
        AssetManager assets(paths, registry, factory, tasks);
        ASSERT_TRUE(assets.scan().succeeded());
        auto first = assets.load_vector_field(handle);
        ASSERT_TRUE(first) << first.error().message;
        EXPECT_EQ(first.value(), assets.load_vector_field(handle).value());
        EXPECT_EQ(factory.texture_creation_count(), 1);
        Scene scene;
        auto emitter = scene.create_entity();
        emitter.add_component<ParticleEmitterComponent>(ParticleEmitterComponent{
            .simulation = ParticleSimulation::GPU, .gpu_dynamics = true, .vector_field = handle});
        const auto components = create_scene_component_registry();
        SceneSerializer serializer(components);
        ASSERT_TRUE(serializer.save(scene, (paths.assets() / "startup.scene").string()));
        ASSERT_TRUE(write_text_file_atomic(paths.root() / "project.json", R"({"version":2,
            "id":"00000000-0000-4000-8000-000000000001","name":"Field export",
            "startup_scene":"startup.scene","input_contexts":[],"input_actions":[]})"));
        auto project = Project::load(paths.root());
        ASSERT_TRUE(project) << project.error();
        auto exported = export_project_assets(project.value(), products.root());
        ASSERT_TRUE(exported) << exported.error();
        std::filesystem::remove_all(paths.root());
        AssetRegistry product_registry;
        AssetManager bundled(products, product_registry, factory, tasks);
        ASSERT_TRUE(bundled.scan().succeeded());
        auto loaded = bundled.load_vector_field(handle);
        ASSERT_TRUE(loaded) << loaded.error().message;
        EXPECT_EQ(loaded.value()->data.values, field.values);
        EXPECT_TRUE(bundled.get_database().is_read_only());
        EXPECT_FALSE(std::filesystem::exists(products.cache()));
    }
}

#include "render/path_trace_scene.h"
#include "render/material/material.h"
#include "render/scene/render_submission.h"
#include "scene/material_parameters.h"

#include <gtest/gtest.h>
#include <limits>

namespace Comet::Tests {
    namespace {
        MeshData triangle_mesh() {
            return {.vertices = {{{-1, -1, 0}, {}, {0, 0, 1}}, {{1, -1, 0}, {}, {0, 0, 1}},
                        {{0, 1, 0}, {}, {0, 0, 1}}},
                .indices = {0, 1, 2}};
        }
        RenderWorld trace_world(const std::shared_ptr<Material>& material) {
            RenderWorld world;
            world.scene_lifetime = 1;
            world.render_items.push_back({.entity_id = 1,
                .mesh_handle = AssetHandle(10),
                .mesh_revision = 1,
                .material = {.material_handle = AssetHandle(11), .resource = material}});
            return world;
        }
    }

    TEST(PathTraceSceneTest, ReusesInputAndKeepsOldSnapshotsAcrossEachChange) {
        auto material = std::make_shared<Material>("surface", "pbr");
        ASSERT_TRUE(material->set_vector_property("base_color", {.2f, .2f, .2f, 1}));
        auto world = trace_world(material);
        auto mesh = triangle_mesh();
        size_t reads = 0;
        uint64_t revision = 1;
        PathTraceSceneBuilder builder;
        const auto read = [&](AssetHandle) {
            ++reads;
            return Result<MeshData, Error>::success(mesh);
        };
        const auto version = [&](AssetHandle) { return revision; };
        auto first = builder.update(world, read, version);
        ASSERT_TRUE(first) << first.error();
        EXPECT_EQ(reads, 1u);
        EXPECT_EQ(builder.update(world, read, version).value(), first.value());
        EXPECT_EQ(reads, 1u);
        world.post_process.exposure = 2;
        EXPECT_EQ(builder.update(world, read, version).value(), first.value());
        ASSERT_TRUE(material->set_vector_property("base_color", {.8f, .1f, .2f, 1}));
        auto changed_material = builder.update(world, read, version);
        ASSERT_TRUE(changed_material);
        EXPECT_NE(changed_material.value(), first.value());
        EXPECT_FLOAT_EQ(changed_material.value()->triangles[0].albedo_metallic.r, .8f);
        EXPECT_NE(first.value()->triangles[0].albedo_metallic.r, .8f);
        world.render_items[0].model_matrix = Math::translate(Math::Mat4(1), {2, 0, 0});
        auto transformed = builder.update(world, read, version).value();
        EXPECT_FLOAT_EQ(transformed->triangles[0].positions[0].x, 1);
        mesh.vertices[0].position.x = -2;
        ++revision;
        auto replaced = builder.update(world, read, version).value();
        EXPECT_FLOAT_EQ(replaced->triangles[0].positions[0].x, 0);
        EXPECT_FLOAT_EQ(transformed->triangles[0].positions[0].x, 1);
        world.lights.push_back({.intensity = 3});
        auto lit = builder.update(world, read, version).value();
        EXPECT_NE(lit, replaced);
        EXPECT_FLOAT_EQ(lit->lighting.lights[0].intensity, 3);
        world.environment.background_color = {.2f, .3f, .4f};
        EXPECT_NE(builder.update(world, read, version).value(), lit);
        builder.reset();
        EXPECT_NE(builder.update(world, read, version).value(), lit);
    }

    TEST(PathTraceSceneTest, PacksBvhWithoutLosingInstancesOrMaterialOverrides) {
        auto material = std::make_shared<Material>("surface", "pbr");
        auto world = trace_world(material);
        auto overrides = std::make_shared<MaterialOverrides>();
        overrides->instance_id = 1;
        overrides->material = AssetHandle(11);
        overrides->vector_properties["base_color"] = {.1f, .5f, .3f, 1};
        overrides->scalar_properties["metallic"] = 1;
        world.render_items[0].material.overrides = overrides;
        for(int i = 1; i < 17; ++i) {
            auto item = world.render_items[0];
            item.entity_id = i + 1;
            item.model_matrix = Math::translate(Math::Mat4(1), {float(i * 3), 0, 0});
            world.render_items.push_back(item);
        }
        int reads = 0;
        PathTraceSceneBuilder builder;
        const auto read = [&](AssetHandle) {
            ++reads;
            return Result<MeshData, Error>::success(triangle_mesh());
        };
        auto scene = builder.update(world, read);
        ASSERT_TRUE(scene) << scene.error();
        EXPECT_EQ(reads, 1);
        EXPECT_EQ(scene.value()->triangles.size(), 17u);
        ASSERT_GT(scene.value()->nodes.size(), 1u);
        EXPECT_EQ(scene.value()->nodes[0].links[3], scene.value()->nodes.size());
        size_t leaves = 0;
        for(size_t i = 0; i < scene.value()->nodes.size(); ++i) {
            const auto& node = scene.value()->nodes[i];
            EXPECT_GT(node.links[3], i);
            EXPECT_LE(node.links[3], scene.value()->nodes.size());
            for(uint32_t j = 0; j < node.links[1]; ++j) {
                ++leaves;
                const auto& triangle = scene.value()->triangles[node.links[0] + j];
                for(const auto p : triangle.positions)
                    for(int axis = 0; axis < 3; ++axis) {
                        EXPECT_GE(p[axis], node.minimum[axis]);
                        EXPECT_LE(p[axis], node.maximum[axis]);
                    }
                EXPECT_EQ(triangle.albedo_metallic, Math::Vec4(.1f, .5f, .3f, 1));
            }
        }
        EXPECT_EQ(leaves, 17u);
        overrides->scalar_properties["metallic"] = 0;
        EXPECT_NE(builder.update(world, read).value(), scene.value());
    }

    TEST(PathTraceSceneTest, RejectsUnsupportedOrInvalidInputsAndRecoversOnPublication) {
        auto material = std::make_shared<Material>("surface", "pbr");
        auto world = trace_world(material);
        auto mesh = triangle_mesh();
        uint64_t revision = 1;
        PathTraceSceneBuilder builder;
        const auto read = [&](AssetHandle) { return Result<MeshData, Error>::success(mesh); };
        const auto update = [&] {
            return builder.update(world, read, [&](AssetHandle) { return revision; });
        };
        ASSERT_TRUE(update());
        mesh.indices[2] = 99;
        ++revision;
        EXPECT_FALSE(update());
        EXPECT_FALSE(update());
        mesh.indices[2] = 2;
        ++revision;
        EXPECT_TRUE(update());
        mesh.skin.resize(3);
        ++revision;
        EXPECT_FALSE(update());
        mesh.skin.clear();
        ++revision;
        ASSERT_TRUE(material->set_surface({.alpha_mode = AlphaMode::Blend}));
        EXPECT_FALSE(update());
        ASSERT_TRUE(material->set_surface({}));
        EXPECT_TRUE(update());
        world.environment.asset = AssetHandle(12);
        world.environment.lighting = true;
        EXPECT_FALSE(update());
        world.environment = {};
        EXPECT_TRUE(update());
        world.render_items[0].model_matrix[0][0] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(update());
        world.render_items[0].model_matrix = Math::Mat4(0);
        EXPECT_FALSE(update());
    }

    TEST(PathTraceSceneTest, ValidatesBoundedQualityAndPreservesAspect) {
        PathTraceSettings settings;
        EXPECT_TRUE(settings.validate());
        EXPECT_EQ(settings.image_size({1920, 1080}), Math::Vec2u(512, 288));
        EXPECT_EQ(settings.image_size({64, 96}), Math::Vec2u(64, 96));
        settings.samples = 0;
        EXPECT_FALSE(settings.validate());
        settings.samples = 4096;
        settings.bounces = 9;
        EXPECT_FALSE(settings.validate());
    }
}

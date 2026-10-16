#include "render/path_trace_scene.h"
#include "render/material/material.h"
#include "render/scene/render_submission.h"
#include "scene/material_parameters.h"
#include "asset/data/texture_sampling.h"
#include "support/render_resource_factory.h"

#include <gtest/gtest.h>
#include <limits>
#include <cstring>

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
        EXPECT_FLOAT_EQ(changed_material.value()->materials->surfaces[0].albedo_metallic.r, .8f);
        EXPECT_NE(first.value()->materials->surfaces[0].albedo_metallic.r, .8f);
        world.render_items[0].model_matrix = Math::translate(Math::Mat4(1), {2, 0, 0});
        auto transformed = builder.update(world, read, version).value();
        EXPECT_FLOAT_EQ(transformed->geometry->triangles[0].positions[0].x, 1);
        mesh.vertices[0].position.x = -2;
        ++revision;
        auto replaced = builder.update(world, read, version).value();
        EXPECT_FLOAT_EQ(replaced->geometry->triangles[0].positions[0].x, 0);
        EXPECT_FLOAT_EQ(transformed->geometry->triangles[0].positions[0].x, 1);
        world.lights.push_back({.intensity = 3});
        auto lit = builder.update(world, read, version).value();
        EXPECT_NE(lit, replaced);
        EXPECT_FLOAT_EQ(lit->lighting->lights[0].intensity, 3);
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
        EXPECT_EQ(scene.value()->geometry->triangles.size(), 17u);
        ASSERT_GT(scene.value()->geometry->nodes.size(), 1u);
        EXPECT_EQ(
            scene.value()->geometry->nodes[0].links[3], scene.value()->geometry->nodes.size());
        size_t leaves = 0;
        for(size_t i = 0; i < scene.value()->geometry->nodes.size(); ++i) {
            const auto& node = scene.value()->geometry->nodes[i];
            EXPECT_GT(node.links[3], i);
            EXPECT_LE(node.links[3], scene.value()->geometry->nodes.size());
            for(uint32_t j = 0; j < node.links[1]; ++j) {
                ++leaves;
                const auto& triangle = scene.value()->geometry->triangles[node.links[0] + j];
                for(const auto p : triangle.positions)
                    for(int axis = 0; axis < 3; ++axis) {
                        EXPECT_GE(p[axis], node.minimum[axis]);
                        EXPECT_LE(p[axis], node.maximum[axis]);
                    }
                EXPECT_EQ(scene.value()->materials->surfaces[triangle.surface[0]].albedo_metallic,
                    Math::Vec4(.1f, .5f, .3f, 1));
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

    TEST(PathTraceSceneTest, TexturesShareDecodedPixelsAndPreserveSourceVersionsAndTangentBasis) {
        TextureData image{.width = 2,
            .height = 1,
            .format = Format::R8G8B8A8_SRGB,
            .pixels = {128, 0, 0, 255, 0, 255, 0, 128},
            .sampler = SamplerDesc{.mag_filter = Filter::Nearest}};
        FakeRenderResourceFactory factory;
        auto texture = factory.try_create_texture(image).value();
        auto material = std::make_shared<Material>("textured", "pbr");
        material->set_texture_property("base_color_texture", texture, AssetHandle(20));
        material->set_texture_property("emissive_texture", texture, AssetHandle(20));
        auto world = trace_world(material);
        world.render_items.push_back(world.render_items.front());
        world.render_items[0].model_matrix = Math::scale(Math::Mat4(1), {-2, 3, 1});
        auto mesh = triangle_mesh();
        for(auto& vertex : mesh.vertices) {
            vertex.texcoord = {.25f, .75f};
            vertex.tangent = {1, 0, 0, 1};
        }
        uint64_t revision = 1;
        int reads = 0;
        PathTraceSceneBuilder::Images images{.texture =
                                                 [&](AssetHandle handle) {
                                                     EXPECT_EQ(handle, AssetHandle(20));
                                                     ++reads;
                                                     return Result<TextureData, Error>::success(
                                                         image);
                                                 },
            .revision = [&](AssetHandle) { return revision; }};
        PathTraceSceneBuilder builder;
        const auto read = [&](AssetHandle) { return Result<MeshData, Error>::success(mesh); };
        const auto first = builder.update(world, read, {}, images);
        ASSERT_TRUE(first) << first.error();
        EXPECT_EQ(reads, 1);
        ASSERT_EQ(first.value()->materials->textures.size(), 2u);
        ASSERT_EQ(first.value()->materials->surfaces.size(), 2u);
        const auto& surface = first.value()->materials->surfaces.front();
        EXPECT_EQ(surface.textures[0], surface.textures[3]);
        const auto& triangle = first.value()->geometry->triangles.front();
        EXPECT_FLOAT_EQ(triangle.normals[0].w, .25f);
        EXPECT_FLOAT_EQ(triangle.tangents[0].w, .75f);
        EXPECT_EQ(Math::Vec3(triangle.tangents[0]), Math::Vec3(-1, 0, 0));
        EXPECT_EQ(Math::Vec3(triangle.bitangents[0]), Math::Vec3(0, 1, 0));
        EXPECT_NEAR(first.value()->materials->texels[0].r, .2158605f, 1e-6f);
        EXPECT_EQ(builder.update(world, read, {}, images).value(), first.value());
        image.pixels[0] = 255;
        ++revision;
        const auto changed = builder.update(world, read, {}, images);
        ASSERT_TRUE(changed);
        EXPECT_NE(changed.value(), first.value());
        EXPECT_FLOAT_EQ(changed.value()->materials->texels[0].r, 1);
        EXPECT_NEAR(first.value()->materials->texels[0].r, .2158605f, 1e-6f);
        auto snapshot = material->snapshot();
        material->set_texture_property("base_color_texture", texture, AssetHandle(21));
        EXPECT_EQ(snapshot->get_texture_source("base_color_texture"), AssetHandle(20));
        EXPECT_EQ(material->get_texture_source("base_color_texture"), AssetHandle(21));
    }

    TEST(CpuTextureImageTest, LinearizesBeforeFilteringAndHonorsAddressingAndBudget) {
        TextureData data{.width = 2,
            .height = 1,
            .format = Format::R8G8B8A8_SRGB,
            .pixels = {128, 0, 0, 128, 0, 255, 0, 255}};
        auto decoded = CpuTextureImage::decode(data, 32);
        ASSERT_TRUE(decoded) << decoded.error();
        auto image = decoded.value();
        EXPECT_NEAR(image.sample({.25f, .5f}).r, .2158605f, 1e-6f);
        EXPECT_FLOAT_EQ(image.sample({.25f, .5f}).a, 128 / 255.f);
        EXPECT_NEAR(image.sample({.5f, .5f}).r, .10793025f, 1e-6f);
        EXPECT_EQ(image.sample({-.25f, .5f}), image.sample({.75f, .5f}));
        image.sampler.address_mode_u = SamplerAddressMode::MirrorRepeat;
        EXPECT_EQ(image.sample({1.25f, .5f}), image.sample({.75f, .5f}));
        image.sampler.address_mode_u = SamplerAddressMode::ClampToBorder;
        EXPECT_EQ(image.sample({-1, .5f}), Math::Vec4(0, 0, 0, 1));
        image.sampler.address_mode_u = SamplerAddressMode::ClampToEdge;
        EXPECT_EQ(image.sample({-1, .5f}), image.pixels[0]);
        EXPECT_FALSE(CpuTextureImage::decode(data, 31));
        data.pixels.pop_back();
        EXPECT_FALSE(CpuTextureImage::decode(data, 32));
        data.cubemap = true;
        EXPECT_FALSE(CpuTextureImage::decode(data, 32));
    }

    TEST(PathTraceSceneTest, BuildsNormalizedEqualAreaEnvironmentAndResetsOnSettingsOrPublication) {
        TextureData cube{.width = 1,
            .height = 1,
            .format = Format::R32G32B32A32_SFLOAT,
            .pixels = std::vector<uint8_t>(6 * sizeof(Math::Vec4)),
            .cubemap = true};
        const Math::Vec4 color{2, 1, .5f, 1};
        for(size_t i = 0; i < 6; ++i)
            std::memcpy(cube.pixels.data() + i * sizeof(color), &color, sizeof(color));
        RenderWorld world;
        world.environment = {
            .asset = AssetHandle(12), .rotation = 90, .lighting = true, .lighting_intensity = 2};
        uint64_t revision = 1;
        bool ready = true;
        PathTraceSceneBuilder::Images images{
            .environment =
                [&](AssetHandle) {
                    if(!ready)
                        return Result<TextureData, Error>::failure({"not ready"});
                    return Result<TextureData, Error>::success(cube);
                },
            .revision = [&](AssetHandle) { return revision; }};
        PathTraceSceneBuilder builder;
        const auto read = [](AssetHandle) { return Result<MeshData, Error>::success({}); };
        const auto first = builder.update(world, read, {}, images);
        ASSERT_TRUE(first) << first.error();
        EXPECT_EQ(first.value()->environment_map->size.x, 16u);
        EXPECT_EQ(first.value()->environment_map->size.y, 8u);
        EXPECT_FLOAT_EQ(first.value()->environment.x, -1);
        EXPECT_FLOAT_EQ(first.value()->environment.y, 2);
        EXPECT_NEAR(first.value()->environment.z, 1, 1e-6f);
        for(size_t i = 0; i < first.value()->environment_map->texels.size(); ++i) {
            const auto pixel = first.value()->environment_map->texels[i];
            EXPECT_EQ(Math::Vec3(pixel), Math::Vec3(color));
            EXPECT_NEAR(
                pixel.w, float(i + 1) / first.value()->environment_map->texels.size(), 1e-6f);
        }
        EXPECT_FLOAT_EQ(first.value()->environment_map->texels.back().w, 1);
        world.environment.lighting_intensity = 3;
        const auto relit = builder.update(world, read, {}, images).value();
        EXPECT_NE(relit, first.value());
        EXPECT_EQ(relit->environment_map, first.value()->environment_map);
        world.environment.rotation = 180;
        const auto rotated = builder.update(world, read, {}, images).value();
        EXPECT_EQ(rotated->environment_map, first.value()->environment_map);
        EXPECT_EQ(builder.statistics().environment_builds, 1u);
        ready = false;
        ++revision;
        EXPECT_FALSE(builder.update(world, read, {}, images));
        ready = true;
        ++revision;
        EXPECT_TRUE(builder.update(world, read, {}, images));
    }

    TEST(PathTraceSceneTest, NonGeometryChangesReuseGeometryAndEnvironment) {
        auto material = std::make_shared<Material>("surface", "pbr");
        auto world = trace_world(material);
        PathTraceSceneBuilder builder;
        size_t reads = 0;
        const auto read = [&](AssetHandle) {
            ++reads;
            return Result<MeshData, Error>::success(triangle_mesh());
        };
        auto first = builder.update(world, read).value();
        world.lights.push_back({.intensity = 2});
        auto lighting = builder.update(world, read).value();
        EXPECT_EQ(lighting->geometry, first->geometry);
        EXPECT_EQ(lighting->materials, first->materials);
        EXPECT_EQ(lighting->emitters, first->emitters);
        EXPECT_EQ(lighting->environment_map, first->environment_map);
        EXPECT_NE(lighting->lighting, first->lighting);
        ASSERT_TRUE(material->set_scalar_property("roughness", .8f));
        auto shading = builder.update(world, read).value();
        EXPECT_EQ(shading->geometry, first->geometry);
        EXPECT_NE(shading->materials, first->materials);
        EXPECT_EQ(shading->lighting, lighting->lighting);
        ASSERT_TRUE(material->set_surface({.double_sided = true}));
        auto double_sided = builder.update(world, read).value();
        EXPECT_EQ(double_sided->geometry, first->geometry);
        EXPECT_FLOAT_EQ(double_sided->materials->surfaces[0].parameters.y, 1);
        EXPECT_EQ(reads, 1u);
        EXPECT_EQ(builder.statistics().geometry_builds, 1u);
        world.render_items[0].model_matrix[3].x = 1;
        auto transformed = builder.update(world, read).value();
        EXPECT_NE(transformed->geometry, first->geometry);
        EXPECT_EQ(transformed->materials, double_sided->materials);
        EXPECT_EQ(builder.statistics().geometry_builds, 2u);
        EXPECT_EQ(reads, 2u);
    }

    TEST(PathTraceSceneTest, SceneIdentitySeparatesSameRevisionMaterials) {
        auto first_material = std::make_shared<Material>("first", "pbr");
        auto second_material = std::make_shared<Material>("second", "pbr");
        ASSERT_TRUE(first_material->set_vector_property("base_color", {1, 0, 0, 1}));
        ASSERT_TRUE(second_material->set_vector_property("base_color", {0, 1, 0, 1}));
        ASSERT_EQ(first_material->get_revision(), second_material->get_revision());
        auto world = trace_world(first_material);
        PathTraceSceneBuilder builder;
        const auto read = [](AssetHandle) {
            return Result<MeshData, Error>::success(triangle_mesh());
        };
        const auto first = builder.update(world, read).value();
        ++world.scene_lifetime;
        world.render_items[0].material.resource = second_material;
        const auto second = builder.update(world, read).value();
        EXPECT_NE(first->materials, second->materials);
        EXPECT_EQ(Math::Vec3(first->materials->surfaces[0].albedo_metallic), Math::Vec3(1, 0, 0));
        EXPECT_EQ(Math::Vec3(second->materials->surfaces[0].albedo_metallic), Math::Vec3(0, 1, 0));
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

#include "asset/data/irradiance_data.h"
#include "asset/data/cubemap_sampling.h"
#include "asset/irradiance_bake.h"
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

#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace Comet::Tests {
    namespace {
        IrradianceData uniform_grid(const Math::Vec3 color) {
            IrradianceData data;
            data.origin = Math::Vec3(0.5f);
            data.probes.resize(8);
            for(auto& probe : data.probes) {
                probe.coefficients[0] = Math::Vec4(color / 0.2820947918f, 1);
                probe.visibility.fill({100, 10000});
            }
            return data;
        }

        void expect_color(
            const Math::Vec3 actual, const Math::Vec3 expected, const float tolerance) {
            for(int channel = 0; channel < 3; ++channel)
                EXPECT_NEAR(actual[channel], expected[channel], tolerance) << "channel " << channel;
        }

        std::shared_ptr<TextureData> constant_environment(const Math::Vec3 color) {
            auto data = std::make_shared<TextureData>();
            data->width = data->height = 1;
            data->format = Format::R32G32B32A32_SFLOAT;
            data->cubemap = true;
            data->pixels.resize(6 * sizeof(Math::Vec4));
            const Math::Vec4 pixel(color, 1);
            for(std::size_t face = 0; face < 6; ++face)
                std::memcpy(data->pixels.data() + face * sizeof(pixel), &pixel, sizeof(pixel));
            return data;
        }

        std::shared_ptr<MeshData> quad(const Math::Vec3 center, const Math::Vec3 u,
            const Math::Vec3 v, const Math::Vec3 normal) {
            auto mesh = std::make_shared<MeshData>();
            for(const auto p : {center - u - v, center + u - v, center + u + v, center - u + v})
                mesh->vertices.push_back(
                    {.position = p, .texcoord = Math::Vec2(0.5f), .normal = normal});
            mesh->indices = {0, 1, 2, 0, 2, 3};
            return mesh;
        }

        IrradianceBake::Settings small_volume() {
            return {.bounds = {{-1, 1, -1}, {1, 3, 1}},
                .spacing = 1,
                .max_distance = 100,
                .ray_samples = 256,
                .bounces = 2};
        }

        IrradianceBake::Input room(
            const IrradianceBake::Material& material, const bool inside = true) {
            IrradianceBake::Input input;
            input.materials.push_back(material);
            const float sign = inside ? 1.0f : -1.0f;
            input.objects = {{quad({0, 0, 0}, {2, 0, 0}, {0, 0, 2}, {0, sign, 0})},
                {quad({0, 4, 0}, {2, 0, 0}, {0, 0, 2}, {0, -sign, 0})},
                {quad({-2, 2, 0}, {0, 2, 0}, {0, 0, 2}, {sign, 0, 0})},
                {quad({2, 2, 0}, {0, 2, 0}, {0, 0, 2}, {-sign, 0, 0})},
                {quad({0, 2, -2}, {2, 0, 0}, {0, 2, 0}, {0, 0, sign})},
                {quad({0, 2, 2}, {2, 0, 0}, {0, 2, 0}, {0, 0, -sign})}};
            return input;
        }

        Math::Vec3 average_radiance(const IrradianceData& data) {
            Math::Vec3 value{0};
            for(const auto& probe : data.probes)
                value += Math::Vec3(probe.coefficients[0]) * 0.2820947918f;
            return value / static_cast<float>(data.probes.size());
        }
    }

    TEST(CpuCubeSamplerTest, SharesFaceOrientationSeamFilteringAndMipOffsets) {
        auto data = constant_environment(Math::Vec3(0));
        data->width = data->height = 2;
        data->mip_levels = 2;
        data->pixels.resize(30 * sizeof(Math::Vec4));
        const std::array<Math::Vec3, 6> colors{
            {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {1, 0, 1}, {0, 1, 1}}};
        for(std::size_t face = 0; face < 6; ++face) {
            for(std::size_t pixel = 0; pixel < 4; ++pixel) {
                const Math::Vec4 value(colors[face], 1);
                std::memcpy(data->pixels.data() + (face * 4 + pixel) * sizeof(value), &value,
                    sizeof(value));
            }
            const Math::Vec4 upper(2, 2, 2, 1);
            std::memcpy(data->pixels.data() + (24 + face) * sizeof(upper), &upper, sizeof(upper));
        }
        const auto checked = CpuCubeSampler::validate(*data);
        ASSERT_TRUE(checked) << checked.error();
        CpuCubeSampler sampler(*data);
        EXPECT_EQ(sampler.decoded_bytes(), 0);
        for(int face = 0; face < 6; ++face)
            expect_color(
                sampler.sample(CpuCubeSampler::face_direction(face, 0, 0)), colors[face], 0);
        EXPECT_EQ(sampler.decoded_bytes(), 24 * sizeof(Math::Vec3));
        expect_color(sampler.sample(glm::normalize(Math::Vec3(1, 1, 0))), {0.5f, 0, 0.5f}, 1e-6f);
        expect_color(sampler.sample({1, 0, 0}, 0.5f), {1.5f, 1, 1}, 0);
        EXPECT_EQ(sampler.decoded_bytes(), checked.value());
        auto invalid = *data;
        invalid.mip_levels = 3;
        EXPECT_FALSE(CpuCubeSampler::validate(invalid));
        invalid = *data;
        invalid.pixels.pop_back();
        EXPECT_FALSE(CpuCubeSampler::validate(invalid));
        invalid = *data;
        const float infinity = std::numeric_limits<float>::infinity();
        std::memcpy(invalid.pixels.data(), &infinity, sizeof(infinity));
        EXPECT_FALSE(CpuCubeSampler::validate(invalid));
    }

    TEST(IrradianceDataTest, UniformRadianceUsesEOverPiAndCoversCellEdges) {
        const auto data = uniform_grid({1, 2, 4});
        ASSERT_TRUE(data.validate());
        for(const auto normal : {Math::Vec3(1, 0, 0), Math::Vec3(0, -1, 0), Math::Vec3(0, 0, 1)}) {
            for(const auto position : {Math::Vec3(0), Math::Vec3(1), Math::Vec3(2)}) {
                const auto sample = data.sample(position, normal);
                ASSERT_TRUE(sample);
                expect_color(*sample, {1, 2, 4}, 1e-5f);
            }
        }
        EXPECT_FALSE(data.sample({-0.01f, 1, 1}, {0, 1, 0}));
        EXPECT_FALSE(data.sample({1, 1, 2.01f}, {0, 1, 0}));
        EXPECT_FALSE(data.sample({1, 1, 1}, Math::Vec3(0)));
        expect_color(data.probe_position(7), Math::Vec3(1.5f), 0);
    }

    TEST(IrradianceDataTest, VisibilityStopsOppositeSideProbeBlending) {
        auto data = uniform_grid({1, 0, 0});
        for(std::size_t index = 0; index < data.probes.size(); ++index) {
            if(index % 2 == 1)
                data.probes[index].coefficients[0] = {0, 0, 1 / 0.2820947918f, 1};
        }
        const auto unoccluded = data.sample({0.9f, 1, 1}, {0, 1, 0});
        ASSERT_TRUE(unoccluded);
        EXPECT_GT(unoccluded->b, 0.1f);
        for(std::size_t index = 1; index < data.probes.size(); index += 2)
            data.probes[index].visibility.fill({0.5f, 0.25f});
        const auto occluded = data.sample({0.9f, 1, 1}, {0, 1, 0});
        ASSERT_TRUE(occluded);
        EXPECT_NEAR(occluded->r, 1, 1e-4f);
        EXPECT_LT(occluded->b, 1e-4f);
        for(auto& probe : data.probes)
            probe.visibility.fill({0, 0});
        const auto blocked = data.sample({1, 1, 1}, {0, 1, 0});
        ASSERT_TRUE(blocked);
        expect_color(*blocked, Math::Vec3(0), 0);
        for(auto& probe : data.probes)
            probe.coefficients[0].w = 0;
        EXPECT_FALSE(data.sample({1, 1, 1}, {0, 1, 0}));
    }

    TEST(IrradianceDataTest, VisibilityIsContinuousAcrossOctahedralTexelsAndSeams) {
        auto data = uniform_grid({1, 0, 0});
        for(std::size_t index = 0; index < data.probes.size(); index += 2) {
            auto& probe = data.probes[index];
            probe.coefficients[0] = {0, 0, 1 / 0.2820947918f, 1};
            for(unsigned y = 0; y < 8; ++y)
                for(unsigned x = 0; x < 8; ++x) {
                    const float distance = (x + y) % 2 ? .2f : 3.0f;
                    probe.visibility[y * 8 + x] = {distance, distance * distance + .04f};
                }
        }
        for(const float z : {.1f, 1.1f}) {
            auto previous = data.sample({.3f, .7f, z}, {0, 1, 0}, 0);
            ASSERT_TRUE(previous);
            float largest_step = 0;
            for(unsigned step = 1; step <= 3600; ++step) {
                const auto current = data.sample({.3f + step * .00025f, .7f, z}, {0, 1, 0}, 0);
                ASSERT_TRUE(current);
                largest_step = std::max(largest_step, glm::length(*current - *previous));
                previous = current;
            }
            EXPECT_LT(largest_step, .01f) << z;
        }
    }

    TEST(IrradianceDataTest, OctahedralDirectionsRoundTripAcrossAllHemispheres) {
        for(int y = 0; y < 17; ++y) {
            for(int x = 0; x < 17; ++x) {
                const Math::Vec2 uv{(x + 0.5f) / 17, (y + 0.5f) / 17};
                const auto direction = IrradianceData::visibility_direction(uv);
                const auto reconstructed = IrradianceData::visibility_coordinate(direction);
                EXPECT_NEAR(reconstructed.x, uv.x, 1e-6f);
                EXPECT_NEAR(reconstructed.y, uv.y, 1e-6f);
                EXPECT_NEAR(glm::length(direction), 1, 1e-6f);
            }
        }
    }

    TEST(IrradianceDataTest, ProductRoundTripsRejectsCorruptionAndPreservesLastValidFile) {
        TemporaryDirectory directory;
        const auto path = directory.path() / "room.gi";
        auto data = uniform_grid({0.3f, 1, 3});
        ASSERT_TRUE(data.save(path));
        const auto bytes = data.encode();
        ASSERT_TRUE(bytes);
        auto loaded = IrradianceData::load(path);
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value().encode().value(), bytes.value());
        auto corrupt = bytes.value();
        corrupt[70] ^= std::byte{1};
        EXPECT_FALSE(IrradianceData::decode(corrupt));
        corrupt = bytes.value();
        corrupt.push_back(std::byte{0});
        EXPECT_FALSE(IrradianceData::decode(corrupt));
        EXPECT_FALSE(IrradianceData::decode(std::span(bytes.value()).first(56)));
        data.probes.front().coefficients[1].x = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(data.save(path));
        EXPECT_EQ(read_binary_file(path, bytes.value().size()).value(), bytes.value());
    }

    TEST(IrradianceDataTest, RejectsImpossibleGridAndDistanceMoments) {
        auto data = uniform_grid(Math::Vec3(1));
        data.dimensions = {64, 64, 64};
        EXPECT_FALSE(data.validate());
        data = uniform_grid(Math::Vec3(1));
        data.spacing.x = 0;
        EXPECT_FALSE(data.validate());
        data = uniform_grid(Math::Vec3(1));
        data.probes[0].visibility[0] = {2, 3};
        EXPECT_FALSE(data.validate());
        data.probes[0].visibility[0] = {-1, 1};
        EXPECT_FALSE(data.validate());
    }

    TEST(IrradianceAssetTest, LoadsOneVersionRejectsStalePublicationAndExportsWithoutSources) {
        TemporaryDirectory directory;
        const ProjectPaths paths(directory.path());
        const auto source = paths.assets() / "baked/room.gi";
        auto data = uniform_grid({1, 2, 4});
        ASSERT_TRUE(data.save(source));
        constexpr AssetHandle handle(80401);
        ASSERT_TRUE(MetadataSerializer{}.save(
            {.handle = handle, .type = AssetType::Irradiance}, metadata_path(source)));
        AssetRegistry registry;
        FakeRenderResourceFactory resources;
        TaskScheduler scheduler(1);
        AssetManager assets(paths, registry, resources, scheduler);
        ASSERT_TRUE(assets.scan().succeeded());
        auto first = assets.load_irradiance(handle);
        ASSERT_TRUE(first) << first.error().message;
        EXPECT_EQ(assets.load_irradiance(handle).value(), first.value());
        EXPECT_EQ(resources.texture_creation_count(), 1);
        ASSERT_TRUE(registry.unregister_asset(handle));
        const auto previous_time = std::filesystem::last_write_time(source);
        resources.on_next_texture_creation([&] {
            data.origin.x += .25f;
            EXPECT_TRUE(data.save(source));
            std::filesystem::last_write_time(source, previous_time + std::chrono::seconds(1));
            EXPECT_TRUE(assets.scan().succeeded());
        });
        EXPECT_FALSE(assets.load_irradiance(handle));
        EXPECT_FALSE(registry.contains(handle));
        auto current = assets.load_irradiance(handle);
        ASSERT_TRUE(current) << current.error().message;
        EXPECT_NE(current.value(), first.value());

        Scene scene;
        auto volume = scene.create_entity("GI");
        volume.add_component<IrradianceVolumeComponent>(
            IrradianceVolumeComponent{.irradiance = handle});
        const auto uuid = volume.get_uuid();
        const auto components = create_scene_component_registry();
        SceneSerializer serializer(components);
        ASSERT_TRUE(serializer.save(scene, (paths.assets() / "startup.scene").string()));
        ASSERT_TRUE(write_text_file_atomic(paths.root() / "project.json", R"({
            "version":2,"id":"00000000-0000-4000-8000-000000000001",
            "name":"GI export","startup_scene":"startup.scene",
            "input_contexts":[],"input_actions":[]})"));
        const auto project = Project::load(paths.root());
        ASSERT_TRUE(project) << project.error();
        TemporaryDirectory destination;
        const ProjectPaths products(destination.path() / "game");
        const auto exported = export_project_assets(project.value(), products.root());
        ASSERT_TRUE(exported) << exported.error();
        std::filesystem::remove_all(paths.root());
        AssetRegistry product_registry;
        AssetManager product_assets(products, product_registry, resources, scheduler);
        ASSERT_TRUE(product_assets.scan().succeeded());
        EXPECT_TRUE(product_assets.get_database().is_read_only());
        auto loaded = product_assets.load_irradiance(handle);
        ASSERT_TRUE(loaded) << loaded.error().message;
        EXPECT_FALSE(std::filesystem::exists(products.cache()));
        const auto reopened = serializer.load((products.assets() / "startup.scene").string());
        ASSERT_TRUE(reopened) << reopened.error();
        const auto saved_volume = reopened.value()->find_entity(uuid);
        ASSERT_TRUE(saved_volume);
        EXPECT_EQ(saved_volume.get_component<IrradianceVolumeComponent>().irradiance, handle);
        ASSERT_TRUE(product_registry.unregister_asset(handle));
        const auto* record = product_assets.get_database().find(handle);
        ASSERT_TRUE(record);
        ASSERT_TRUE(write_text_file_atomic(products.assets() / record->path, "damaged"));
        EXPECT_FALSE(product_assets.load_irradiance(handle));
    }

    TEST(IrradianceBakeTest, EmptyScenePreservesUniformHdrEnvironmentAndDeterministicProducts) {
        IrradianceBake::Input input;
        input.environment = constant_environment({1, 2, 4});
        auto first = IrradianceBake::bake(input, small_volume());
        ASSERT_TRUE(first) << first.error();
        EXPECT_EQ(first.value().statistics.active_probes, 8);
        EXPECT_EQ(first.value().statistics.triangles, 0);
        EXPECT_EQ(first.value().statistics.rays, 8u * (256u + 256u));
        for(const auto normal : {Math::Vec3(0, 1, 0), Math::Vec3(1, 0, 0), Math::Vec3(0, 0, -1)}) {
            const auto sample = first.value().data.sample({0, 2, 0}, normal);
            ASSERT_TRUE(sample);
            expect_color(*sample, {1, 2, 4}, 0.012f);
        }
        auto second = IrradianceBake::bake(input, small_volume());
        ASSERT_TRUE(second);
        EXPECT_EQ(first.value().data.encode().value(), second.value().data.encode().value());
    }

    TEST(IrradianceBakeTest, DiffusePlaneMatchesAnalyticalDirectionalLightingAndShadowOcclusion) {
        IrradianceBake::Input input;
        input.materials.push_back({.base_color = Math::Vec4(0.5f, 0.5f, 0.5f, 1)});
        input.objects.push_back({quad({0, 0, 0}, {10000, 0, 0}, {0, 0, 10000}, {0, 1, 0})});
        input.lights.push_back({.direction = {0, -1, 0}, .intensity = Math::PI});
        auto settings = small_volume();
        settings.bounds = {{-0.5f, 0.5f, -0.5f}, {0.5f, 1.5f, 0.5f}};
        settings.max_distance = 1000;
        settings.ray_samples = 512;
        settings.bounces = 1;
        const auto lit = IrradianceBake::bake(input, settings);
        ASSERT_TRUE(lit) << lit.error();
        const auto down = lit.value().data.sample({0, 1, 0}, {0, -1, 0});
        const auto horizontal = lit.value().data.sample({0, 1, 0}, {1, 0, 0});
        ASSERT_TRUE(down);
        ASSERT_TRUE(horizontal);
        expect_color(*down, Math::Vec3(0.48f), 0.008f);
        expect_color(*horizontal, Math::Vec3(0.24f), 0.008f);
        input.materials.push_back({.base_color = {0, 0, 0, 1}});
        input.objects.push_back(
            {quad({0, 2, 0}, {10000, 0, 0}, {0, 0, 10000}, {0, -1, 0}), Math::Mat4(1), 1});
        const auto blocked = IrradianceBake::bake(input, settings);
        ASSERT_TRUE(blocked) << blocked.error();
        expect_color(average_radiance(blocked.value().data), Math::Vec3(0), 1e-6f);
    }

    TEST(IrradianceBakeTest, ClosedEmissiveRoomMatchesMultiBounceEnergyReference) {
        auto input = room({.base_color = {0.5f, 0.5f, 0.5f, 1}, .emissive = Math::Vec3(1)});
        auto settings = small_volume();
        float reference = 0;
        for(std::uint32_t bounces = 0; bounces <= 4; ++bounces) {
            settings.bounces = bounces;
            reference += std::pow(0.48f, float(bounces));
            const auto baked = IrradianceBake::bake(input, settings);
            ASSERT_TRUE(baked) << baked.error();
            EXPECT_EQ(baked.value().statistics.active_probes, 8);
            expect_color(average_radiance(baked.value().data), Math::Vec3(reference), 0.001f);
            const auto sample = baked.value().data.sample({0, 2, 0}, {0, 1, 0});
            ASSERT_TRUE(sample);
            expect_color(*sample, Math::Vec3(reference), 0.012f);
        }
        input.materials[0].metallic = 1;
        const auto metal = IrradianceBake::bake(input, settings);
        ASSERT_TRUE(metal);
        expect_color(average_radiance(metal.value().data), Math::Vec3(1), 0.001f);
    }

    TEST(IrradianceBakeTest, ColoredWallsContributeAdditionalDiffuseBounceEnergy) {
        auto input = room({.base_color = {0.65f, 0.65f, 0.65f, 1}});
        input.materials.push_back({.base_color = {0.9f, 0.04f, 0.04f, 1}});
        input.objects[2].material = 1;
        input.lights.push_back(
            {.type = LightType::Point, .position = {0, 3, 0}, .intensity = 30, .range = 20});
        auto settings = small_volume();
        settings.ray_samples = 2048;
        settings.bounces = 1;
        const auto single = IrradianceBake::bake(input, settings);
        settings.bounces = 3;
        const auto multiple = IrradianceBake::bake(input, settings);
        ASSERT_TRUE(single) << single.error();
        ASSERT_TRUE(multiple) << multiple.error();
        const auto first = average_radiance(single.value().data);
        const auto final = average_radiance(multiple.value().data);
        const auto indirect = final - first;
        RecordProperty("one_bounce_red", std::to_string(first.r));
        RecordProperty("three_bounce_red", std::to_string(final.r));
        RecordProperty("three_bounce_green", std::to_string(final.g));
        RecordProperty("three_bounce_seconds", std::to_string(multiple.value().statistics.seconds));
        RecordProperty("three_bounce_rays", std::to_string(multiple.value().statistics.rays));
        RecordProperty("working_bytes", std::to_string(multiple.value().statistics.working_bytes));
        EXPECT_GT(indirect.r, 0.02f);
        EXPECT_GT(indirect.r, indirect.g * 1.2f)
            << "first " << first.r << ',' << first.g << "; final " << final.r << ',' << final.g;
        EXPECT_GT(indirect.g, 0.005f);
    }

    TEST(IrradianceBakeTest, RealVisibilityKeepsAdjacentRoomsFromExchangingLight) {
        auto left = room({.base_color = {0, 0, 0, 1}, .emissive = {1, 0, 0}});
        auto right = room({.base_color = {0, 0, 0, 1}, .emissive = {0, 0, 1}});
        left.materials.push_back(right.materials.front());
        for(auto& object : left.objects)
            object.transform = Math::compose_trs({-1.1f, 0, 0}, Math::Vec3(0), {0.45f, 1, 1});
        for(auto object : right.objects) {
            object.transform = Math::compose_trs({1.1f, 0, 0}, Math::Vec3(0), {0.45f, 1, 1});
            object.material = 1;
            left.objects.push_back(std::move(object));
        }
        auto settings = small_volume();
        settings.bounds = {{-2, 0, -2}, {2, 4, 2}};
        settings.spacing = 2;
        settings.bounces = 0;
        const auto baked = IrradianceBake::bake(left, settings);
        ASSERT_TRUE(baked) << baked.error();
        auto unoccluded = baked.value().data;
        for(auto& probe : unoccluded.probes)
            probe.visibility.fill({100, 10000});
        for(const float side : {-1.0f, 1.0f}) {
            const Math::Vec3 point{side * 0.3f, 2, 0};
            const auto visible = baked.value().data.sample(point, {0, 1, 0});
            const auto leaking = unoccluded.sample(point, {0, 1, 0});
            ASSERT_TRUE(visible);
            ASSERT_TRUE(leaking);
            const auto own = side < 0 ? 0 : 2;
            const auto other = side < 0 ? 2 : 0;
            EXPECT_GT((*visible)[own], 0.95f);
            EXPECT_LT((*visible)[other], 0.03f);
            EXPECT_GT((*leaking)[other], 0.2f);
        }
    }

    TEST(IrradianceBakeTest, NonIndexedGeometryAndMirroredNonUniformTransformsRetainEnergy) {
        auto input = room({.base_color = {0, 0, 0, 1}, .emissive = {1, 2, 4}});
        const auto transform = Math::compose_trs({10, 0, -5}, Math::Vec3(0), {-2, 0.5f, 3});
        for(auto& object : input.objects) {
            auto expanded = std::make_shared<MeshData>();
            for(const auto index : object.mesh->indices)
                expanded->vertices.push_back(object.mesh->vertices[index]);
            object.mesh = std::move(expanded);
            object.transform = transform;
        }
        auto settings = small_volume();
        settings.bounds = {{8, 0.5f, -8}, {12, 1.5f, -2}};
        settings.spacing = 2;
        const auto baked = IrradianceBake::bake(input, settings);
        ASSERT_TRUE(baked) << baked.error();
        EXPECT_EQ(baked.value().statistics.triangles, 12);
        EXPECT_GT(baked.value().statistics.bvh_nodes, 1);
        EXPECT_EQ(baked.value().statistics.active_probes, baked.value().data.probes.size());
        expect_color(average_radiance(baked.value().data), {1, 2, 4}, 0.001f);
    }

    TEST(IrradianceBakeTest, ClosedGeometryBlocksEnvironmentAndRejectsProbesInsideSolids) {
        auto input = room({.base_color = {0, 0, 0, 1}});
        input.environment = constant_environment(Math::Vec3(4));
        const auto closed = IrradianceBake::bake(input, small_volume());
        ASSERT_TRUE(closed) << closed.error();
        EXPECT_EQ(closed.value().statistics.active_probes, 8);
        expect_color(average_radiance(closed.value().data), Math::Vec3(0), 0);
        input.objects.erase(input.objects.begin() + 1);
        const auto open = IrradianceBake::bake(input, small_volume());
        ASSERT_TRUE(open);
        EXPECT_GT(average_radiance(open.value().data).r, 0.1f);
        auto solid = room({.base_color = Math::Vec4(1)}, false);
        solid.environment = input.environment;
        const auto invalid = IrradianceBake::bake(solid, small_volume());
        ASSERT_TRUE(invalid) << invalid.error();
        EXPECT_EQ(invalid.value().statistics.active_probes, 0);
        EXPECT_FALSE(invalid.value().data.sample({0, 2, 0}, {0, 1, 0}));
    }

    TEST(IrradianceBakeTest, AlphaMaskAndBlendDoNotBecomeOpaqueBakeOccluders) {
        IrradianceBake::Input input;
        input.environment = constant_environment(Math::Vec3(1));
        input.materials.push_back(
            {.base_color = {0, 0, 0, 0}, .surface = {.alpha_mode = AlphaMode::Mask}});
        input.objects.push_back({quad({0, 4, 0}, {1000, 0, 0}, {0, 0, 1000}, {0, -1, 0})});
        auto settings = small_volume();
        settings.bounces = 0;
        auto masked = IrradianceBake::bake(input, settings);
        ASSERT_TRUE(masked);
        expect_color(average_radiance(masked.value().data), Math::Vec3(1), 0.001f);
        input.materials[0].surface.alpha_mode = AlphaMode::Opaque;
        auto opaque = IrradianceBake::bake(input, settings);
        ASSERT_TRUE(opaque);
        EXPECT_LT(average_radiance(opaque.value().data).r, 0.55f);
        input.materials[0].surface.alpha_mode = AlphaMode::Blend;
        auto blend = IrradianceBake::bake(input, settings);
        ASSERT_TRUE(blend);
        expect_color(average_radiance(blend.value().data), Math::Vec3(1), 0.001f);

        auto cutout = std::make_shared<TextureData>();
        cutout->width = cutout->height = 1;
        cutout->pixels = {255, 255, 255, 0};
        cutout->sampler = SamplerDesc{};
        cutout->sampler->address_mode_u = SamplerAddressMode::ClampToBorder;
        auto outside_uv = std::make_shared<MeshData>(*input.objects[0].mesh);
        for(auto& vertex : outside_uv->vertices)
            vertex.texcoord = {1.5f, 0.5f};
        input.objects[0].mesh = outside_uv;
        input.materials[0].base_color.a = 1;
        input.materials[0].surface.alpha_mode = AlphaMode::Mask;
        input.materials[0].base_color_texture = cutout;
        const auto border = IrradianceBake::bake(input, settings);
        ASSERT_TRUE(border);
        expect_color(
            average_radiance(border.value().data), average_radiance(opaque.value().data), 1e-6f);
        for(const auto mode : {SamplerAddressMode::Repeat, SamplerAddressMode::MirrorRepeat,
                SamplerAddressMode::ClampToEdge}) {
            cutout->sampler->address_mode_u = mode;
            const auto transparent = IrradianceBake::bake(input, settings);
            ASSERT_TRUE(transparent);
            expect_color(average_radiance(transparent.value().data), Math::Vec3(1), 0.001f);
        }
    }

    TEST(IrradianceBakeTest, EmissionTexturesAreDecodedToLinearLight) {
        auto texture = std::make_shared<TextureData>();
        texture->width = texture->height = 1;
        texture->format = Format::R8G8B8A8_SRGB;
        texture->pixels = {128, 64, 32, 255};
        auto input = room(
            {.base_color = {0, 0, 0, 1}, .emissive = Math::Vec3(1), .emissive_texture = texture});
        auto settings = small_volume();
        settings.bounces = 0;
        const auto baked = IrradianceBake::bake(input, settings);
        ASSERT_TRUE(baked) << baked.error();
        expect_color(average_radiance(baked.value().data), {0.21586f, 0.05127f, 0.01444f}, 0.0001f);
    }

    TEST(IrradianceBakeTest, RejectsInvalidInputsAndCancelsWithoutPublishingPartialProducts) {
        IrradianceBake::Input input;
        input.environment = constant_environment(Math::Vec3(1));
        auto settings = small_volume();
        std::uint32_t last = 0;
        const auto cancelled = IrradianceBake::bake(
            input, settings, [&](const std::uint32_t completed, const std::uint32_t total) {
                EXPECT_GE(completed, last);
                EXPECT_EQ(total, 8);
                last = completed;
                return completed < 2;
            });
        EXPECT_FALSE(cancelled);
        EXPECT_EQ(last, 2);
        settings.spacing = 0.001f;
        EXPECT_FALSE(IrradianceBake::bake(input, settings));
        settings = small_volume();
        settings.bounces = 5;
        EXPECT_FALSE(IrradianceBake::bake(input, settings));
        settings = small_volume();
        input.materials.emplace_back();
        auto mesh = quad({0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0});
        mesh->indices[0] = 100;
        input.objects.push_back({mesh});
        EXPECT_FALSE(IrradianceBake::bake(input, settings));
        input.objects.clear();
        auto invalid_texture = constant_environment(Math::Vec3(1));
        invalid_texture->pixels.clear();
        input.environment = invalid_texture;
        EXPECT_FALSE(IrradianceBake::bake(input, settings));
    }
}

#include "support/render_gpu_test.h"
#include "asset/data/mesh_data.h"
#include "render/material/material.h"
#include "asset/registry.h"
#include "asset/data/texture_data.h"
#include "render/resource/mesh.h"
#include "asset/data/animation_library.h"
#include "render/passes/skinning_pass.h"
#include "render/scene/render_geometry.h"
namespace Comet::Tests {
    class SkinningGpuTest: public RenderGpuTest {
    protected:
        MeshData m_source;
        RenderScene m_scene;
        std::unique_ptr<FrameScheduler> m_readback_frames;
        std::shared_ptr<Readback> m_readback;
        static constexpr Math::Vec2u size{160, 120};

        void SetUp() override {
            RenderGpuTest::SetUp();
            ASSERT_FALSE(HasFatalFailure());
            ASSERT_TRUE(prepare_offscreen_host(size));
            auto skeleton = std::make_shared<AnimationData>();
            skeleton->mesh_node = 0;
            skeleton->nodes = {{"Mesh"}, {"Bone"}};
            skeleton->evaluation_order = {0, 1};
            skeleton->joints = {
                {.node = 1, .bounds = BoundingBox{{-0.25f, -0.25f, 0}, {0.25f, 0.25f, 0}}}};
            skeleton->clips = {{.name = "move",
                .duration = 1,
                .channels = {{.node = 1,
                    .path = AnimationChannel::Path::Translation,
                    .interpolation = AnimationChannel::Interpolation::Linear,
                    .times = {0, 1},
                    .values = {{-0.5f, 0, 0, 0}, {0.5f, 0, 0, 0}}}}}};
            MeshData data{.vertices = {{{-0.25f, -0.25f, 0}, {}, {0, 0, 1}},
                              {{0.25f, -0.25f, 0}, {}, {0, 0, 1}}, {{0, 0.25f, 0}, {}, {0, 0, 1}}},
                .indices = {0, 1, 2},
                .skin = std::vector<SkinInfluences>(3, {{0, 0, 0, 0}, {1, 0, 0, 0}}),
                .animation = skeleton};
            m_source = data;
            auto uploaded = engine->get_render_resources().try_create_mesh(data);
            ASSERT_TRUE(uploaded);
            auto material = std::make_shared<Material>("Animated", "unlit_color");
            ASSERT_TRUE(material->set_vector_property("color", {1, 0, 0, 1}));
            auto& assets = engine->get_asset_registry();
            ASSERT_TRUE(assets.register_asset(AssetHandle(10), uploaded.value()));
            ASSERT_TRUE(assets.register_asset(AssetHandle(11), material));
            m_scene = {};
            m_scene.cameras.push_back({.primary = true,
                .view_matrix = Math::translate(Math::Mat4(1), {0, 0, -3}),
                .projection = RenderCamera::Projection::Orthographic,
                .orthographic_height = 2});
            m_scene.render_items.push_back({.entity_id = 1,
                .mesh_handle = AssetHandle(10),
                .material_handle = AssetHandle(11)});
            auto& device = engine->get_renderer().get_render_context().get_device();
            m_readback_frames = std::make_unique<FrameScheduler>(device, 1);
            m_readback_frames->initialize_swapchain_images(1);
            m_readback = std::make_shared<Readback>(device,
                engine->get_renderer().get_render_context().get_context().get_physical_device(),
                size.x * size.y * 4);
        }
        void TearDown() override {
            m_readback_frames.reset();
            m_readback.reset();
            RenderGpuTest::TearDown();
        }
        double centroid() {
            render_and_copy(engine->get_renderer(), m_scene, *m_readback_frames, m_readback);
            m_readback_frames->wait_for_all_slots();
            const auto pixels = m_readback->read();
            double total = 0;
            std::size_t count = 0;
            for(uint32_t y = 0; y < size.y; ++y)
                for(uint32_t x = 0; x < size.x; ++x) {
                    const auto index = (y * size.x + x) * 4;
                    if(std::max(std::to_integer<int>(pixels[index]),
                           std::to_integer<int>(pixels[index + 2]))
                            > 150
                        && std::to_integer<int>(pixels[index + 1]) < 40) {
                        total += x;
                        ++count;
                    }
                }
            EXPECT_GT(count, 50u);
            return count ? total / count : 0;
        }

        double centroid(const double time) {
            m_scene.render_items[0].animation = std::make_shared<const AnimationPlayback>(
                AnimationPlayback{.current = {.clip = "move", .time = time, .loop = false}});
            return centroid();
        }
        void consume_missing_clip_diagnostics(const std::string& clip) {
            std::istringstream log(messages.str());
            std::string line;
            std::size_t count = 0;
            while(std::getline(log, line)) {
                if(line.empty())
                    continue;
                EXPECT_NE(line.find("Animation clip is missing: " + clip), std::string::npos)
                    << line;
                ++count;
            }
            EXPECT_GT(count, 0u);
            EXPECT_LE(count, Config{}.render.max_frames_in_flight);
            messages.str("");
            messages.clear();
        }
    };
    TEST_F(SkinningGpuTest, SkinnedNormalMapMatchesBakedTangentsAcrossBoneTransforms) {
        auto& assets = engine->get_asset_registry();
        auto& resources = engine->get_render_resources();
        const auto normal =
            resources.try_create_texture({.width = 1, .height = 1, .pixels = {204, 179, 230, 255}});
        ASSERT_TRUE(normal);
        auto material = std::make_shared<Material>("mapped skin", "pbr");
        ASSERT_TRUE(material->set_vector_property("base_color", {.6f, .4f, .2f, 1}));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        ASSERT_TRUE(material->set_surface({.double_sided = true}));
        material->set_texture_property("normal_texture", normal.value());
        ASSERT_TRUE(assets.replace_asset(AssetHandle(11), material));
        m_scene.lights = {{.direction = {-.6f, -.3f, -.8f}, .intensity = Math::PI},
            {.direction = {-.6f, -.3f, .8f}, .intensity = Math::PI}};
        const auto capture = [&] {
            render_and_copy(engine->get_renderer(), m_scene, *m_readback_frames, m_readback);
            m_readback_frames->wait_for_all_slots();
            return m_readback->read();
        };
        for(const float scale : {1.5f, -1.5f}) {
            SCOPED_TRACE(scale);
            auto source = m_source;
            // 镜像骨骼改变三角形绕序；此例保持变形后的正面朝向相机，单独验证切线基。
            if(scale < 0)
                std::swap(source.indices[1], source.indices[2]);
            for(auto& vertex : source.vertices)
                vertex.tangent = {0, 1, 0, -1};
            auto skeleton = std::make_shared<AnimationData>(*source.animation);
            skeleton->clips.clear();
            skeleton->nodes[1].rest.rotation = glm::angleAxis(Math::PI / 4, Math::Vec3(0, 0, 1));
            skeleton->nodes[1].rest.scale = {scale, .75f, 1};
            source.animation = skeleton;
            const auto skinned = resources.try_create_mesh(source);
            ASSERT_TRUE(skinned);
            ASSERT_TRUE(assets.replace_asset(AssetHandle(10), skinned.value()));
            m_scene.render_items[0].mesh_handle = AssetHandle(10);
            const auto actual = capture();
            const auto transform =
                Math::scale(glm::mat4_cast(skeleton->nodes[1].rest.rotation), {scale, .75f, 1});
            const auto basis = Math::Mat3(transform);
            auto baked = source;
            baked.animation.reset();
            baked.skin.clear();
            for(auto& vertex : baked.vertices) {
                vertex.position = Math::Vec3(transform * Math::Vec4(vertex.position, 1));
                vertex.normal = glm::normalize(glm::transpose(glm::inverse(basis)) * vertex.normal);
                vertex.tangent = Math::Vec4(basis * Math::Vec3(vertex.tangent),
                    vertex.tangent.w * (scale < 0 ? -1.f : 1.f));
            }
            const auto reference = resources.try_create_mesh(baked);
            ASSERT_TRUE(reference);
            ASSERT_TRUE(assets.replace_asset(AssetHandle(10), reference.value()));
            const auto expected = capture();
            ASSERT_EQ(actual.size(), expected.size());
            int maximum_error = 0;
            for(size_t index = 0; index < actual.size(); ++index)
                maximum_error =
                    std::max(maximum_error, std::abs(std::to_integer<int>(actual[index])
                                                     - std::to_integer<int>(expected[index])));
            EXPECT_LE(maximum_error, 2);
            // 同时确认图像受切线法线影响，防止两条路径均未绘制而误通过。
            ASSERT_TRUE(material->set_scalar_property("normal_scale", 0));
            const auto geometric = capture();
            size_t changed = 0;
            for(size_t index = 0; index < actual.size(); ++index)
                changed += std::abs(std::to_integer<int>(actual[index])
                                    - std::to_integer<int>(geometric[index]))
                           > 4;
            EXPECT_GT(changed, 30u);
            ASSERT_TRUE(material->set_scalar_property("normal_scale", 1));
        }
    }

    TEST_F(SkinningGpuTest, GpuPoseMovesPixelsAndSharedMeshInstancesKeepIndependentPoses) {
        const double first = centroid(0);
        const double second = centroid(1);
        EXPECT_GT(second - first, 45);
        m_scene.render_items.push_back(m_scene.render_items.front());
        m_scene.render_items[1].entity_id = 2;
        m_scene.render_items[1].animation = std::make_shared<AnimationPlayback>(
            AnimationPlayback{.current = {.clip = "move", .time = 1, .loop = false}});
        const double both = centroid(0);
        EXPECT_NEAR(both, (first + second) / 2, 1);
        m_scene.render_items.pop_back();
        EXPECT_NEAR(centroid(0), first, 1);

        auto plain = m_source;
        plain.skin.clear();
        plain.animation.reset();
        const auto uploaded = engine->get_render_resources().try_create_mesh(plain);
        ASSERT_TRUE(uploaded);
        ASSERT_TRUE(engine->get_asset_registry().register_asset(AssetHandle(12), uploaded.value()));
        m_scene.render_items[0].mesh_handle = AssetHandle(12);
        for(int frame = 0; frame < 4; ++frame)
            EXPECT_NEAR(centroid(), (first + second) / 2, 1);
        m_scene.render_items[0].mesh_handle = AssetHandle(10);
        for(int frame = 0; frame < 4; ++frame)
            EXPECT_NEAR(centroid(1), second, 1);
    }
    TEST_F(SkinningGpuTest, UnchangedActualPoseReusesSafeSlotsAndMovementStillUpdatesBounds) {
        auto& renderer = engine->get_renderer();
        const auto slots = Config{}.render.max_frames_in_flight;
        const double start = centroid(0);
        const double initial = centroid(0.25);
        for(uint32_t frame = 0; frame < slots * 2; ++frame)
            EXPECT_NEAR(centroid(0.25), initial, 1);
        auto statistics = renderer.get_skinning_statistics();
        EXPECT_EQ(statistics.dispatches, 0u);
        EXPECT_EQ(statistics.reused_outputs, 1u);
        EXPECT_EQ(statistics.palette_upload_bytes, 0u);

        // 包装对象、采样时间变化但实际骨骼矩阵相同，也不重发 dispatch。
        m_scene.render_items[0].animation = std::make_shared<const AnimationPlayback>(
            AnimationPlayback{.current = {.clip = "move", .time = 2, .loop = false}});
        const double endpoint = centroid();
        for(uint32_t frame = 0; frame < slots * 2; ++frame)
            EXPECT_NEAR(centroid(3), endpoint, 1);
        EXPECT_EQ(renderer.get_skinning_statistics().dispatches, 0u);
        EXPECT_EQ(renderer.get_skinning_statistics().reused_outputs, 1u);

        m_scene.render_items[0].model_matrix = Math::translate(Math::Mat4(1), {0.25f, 0, 0});
        EXPECT_GT(centroid() - endpoint, 10);
        EXPECT_EQ(renderer.get_skinning_statistics().dispatches, 0u);
        m_scene.render_items[0].model_matrix = Math::translate(Math::Mat4(1), {10, 0, 0});
        render_and_copy(renderer, m_scene, *m_readback_frames, m_readback);
        EXPECT_EQ(renderer.get_skinning_statistics().dispatches, 0u);
        m_readback_frames->wait_for_all_slots();
        const auto hidden = m_readback->read();
        for(size_t index = 0; index < hidden.size(); index += 4)
            EXPECT_LT(std::to_integer<int>(hidden[index]), 10);

        m_scene.render_items[0].model_matrix = Math::Mat4(1);
        EXPECT_LT(centroid(0), endpoint - 40);
        EXPECT_EQ(renderer.get_skinning_statistics().dispatches, 1u);
        EXPECT_GT(renderer.get_skinning_statistics().palette_upload_bytes, 0u);
        auto replacement = engine->get_render_resources().try_create_mesh(m_source);
        ASSERT_TRUE(replacement);
        ASSERT_TRUE(
            engine->get_asset_registry().replace_asset(AssetHandle(10), replacement.value()));
        EXPECT_NEAR(centroid(0), start, 1);
        EXPECT_EQ(renderer.get_skinning_statistics().dispatches, 1u);
    }

    TEST_F(SkinningGpuTest, ReusedOutputRecomputesConcreteWorldBounds) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        const auto source = engine->get_render_resources().try_create_mesh(m_source);
        ASSERT_TRUE(source);
        FrameScheduler frames(device, 1);
        frames.initialize_swapchain_images(1);
        FrameWait wait{device, frames};
        SkinningPass skinning(device, 1);
        RenderWorld world{.render_items = {{.entity_id = 1, .mesh = source.value()}}};
        RenderGeometry geometry;
        for(uint32_t index = 0; index < 3; ++index) {
            world.render_items[0].model_matrix =
                Math::translate(Math::Mat4(1), {float(index * 10), 0, 0});
            geometry.prepare(world);
            begin_frame(frames);
            std::vector<QueueSemaphoreSubmit> waits;
            ASSERT_TRUE(skinning.prepare(world, frames, geometry, waits));
            ASSERT_EQ(geometry.get_items().size(), 1u);
            ASSERT_NE(geometry.get_items()[0].world_bounds, nullptr);
            EXPECT_NEAR(geometry.get_items()[0].world_bounds->center().x, index * 10.f, 1e-5f);
            EXPECT_EQ(skinning.get_statistics().dispatches, index == 0 ? 1u : 0u);
            EXPECT_EQ(skinning.get_statistics().reused_outputs, index == 0 ? 0u : 1u);
            submit(device, frames, waits);
        }
        frames.wait_for_all_slots();
    }

    TEST_F(SkinningGpuTest, MissingClipUsesBindPoseAndReplacementInvalidatesPausedPose) {
        const double first = centroid(0);
        const double second = centroid(1);
        m_scene.render_items[0].animation = std::make_shared<const AnimationPlayback>(
            AnimationPlayback{.current = {.clip = "missing", .time = 1, .loop = false}});
        const double bind = centroid();
        EXPECT_NEAR(bind, (first + second) / 2, 1);
        for(int frame = 0; frame < 6; ++frame)
            EXPECT_NEAR(centroid(), bind, 1);
        consume_missing_clip_diagnostics("missing");

        auto replacement = m_source;
        auto clips = std::make_shared<AnimationData>(*replacement.animation);
        clips->clips.front().name = "missing";
        replacement.animation = std::move(clips);
        const auto uploaded =
            engine->get_render_resources().try_create_mesh(std::move(replacement));
        ASSERT_TRUE(uploaded);
        ASSERT_TRUE(engine->get_asset_registry().replace_asset(AssetHandle(10), uploaded.value()));
        for(int frame = 0; frame < 4; ++frame)
            EXPECT_NEAR(centroid(), second, 1);
        EXPECT_FALSE(m_source.animation->find_clip("missing"));
        EXPECT_TRUE(messages.str().empty()) << messages.str();
    }
    TEST_F(SkinningGpuTest, SynchronizedBlendSpaceAndTransitionReachSkinnedVertices) {
        // 当前两个片段的半周期位置为 0 与 .25；再与旧位置 -.5 混合得到 -.1875。
        const double expected = centroid(.3125);
        auto data = std::make_shared<AnimationData>(*m_source.animation);
        auto second = data->clips.front();
        second.name = "long";
        second.duration = 2;
        second.channels.front().times.back() = 2;
        second.channels.front().values.back().x *= 2;
        data->clips.push_back(second);
        m_source.animation = data;
        auto mesh = engine->get_render_resources().try_create_mesh(m_source);
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(engine->get_asset_registry().replace_asset(AssetHandle(10), mesh.value()));
        const auto frozen = std::make_shared<const AnimationPlayback>(AnimationPlayback{
            .current =
                {.clip = "move", .time = .5, .blends = {{"long", .5f}}, .normalized_time = true},
            .previous = {.clip = "move", .time = 0},
            .blend = .5f});
        m_scene.render_items[0].animation = frozen;
        for(int frame = 0; frame < 4; ++frame)
            EXPECT_NEAR(centroid(), expected, 1);
        EXPECT_EQ(frozen->current.time, .5);
        EXPECT_TRUE(messages.str().empty()) << messages.str();
    }

    TEST_F(SkinningGpuTest, LibraryTwoDimensionalBlendAndMaskedLayerReachTheSameGpuPose) {
        // 三个同步片段得到 .15，40% 的覆盖层将其推进至 .29。
        const double expected = centroid(.79);
        auto source = std::make_shared<AnimationData>(*m_source.animation);
        auto second = source->clips.front();
        second.name = "long";
        second.duration = 2;
        second.channels.front().times.back() = 2;
        second.channels.front().values.back().x = 1;
        auto third = source->clips.front();
        third.name = "third";
        third.channels.front().values.back().x = 1.5f;
        source->clips.push_back(second);
        source->clips.push_back(third);
        AnimationLibrary library;
        library.entries = {{"a", AssetHandle(7), "move"}, {"b", AssetHandle(7), "long"},
            {"c", AssetHandle(7), "third"}};
        library.sources = {{AssetHandle(7), source}};
        const auto target = m_source.animation;
        auto bound = library.bind(*target);
        ASSERT_TRUE(bound) << bound.error();
        const std::vector<AnimationBlendPoint> points{{0, "a", 0}, {1, "b", 0}, {0, "c", 1}};
        auto blend = blend_animation_2d(points, {.25f, .25f});
        ASSERT_TRUE(blend);
        AnimationPlayback frame{.current = blend.value()};
        frame.current.time = .5;
        frame.clips = std::make_shared<const AnimationData>(std::move(bound).value());
        frame.clips_target = target;
        frame.layers = {{{.clip = "a", .time = 1, .loop = false}, .4f, {"Bone"}}};
        m_scene.render_items[0].animation = std::make_shared<const AnimationPlayback>(frame);
        for(int i = 0; i < 4; ++i)
            EXPECT_NEAR(centroid(), expected, 1);
        auto pose = sample_animation(*target, &frame);
        ASSERT_TRUE(pose);
        frame.pose_source = target;
        frame.pose = std::make_shared<const AnimationPose>(std::move(pose).value());
        m_scene.render_items[0].animation = std::make_shared<const AnimationPlayback>(frame);
        for(int i = 0; i < 4; ++i)
            EXPECT_NEAR(centroid(), expected, 1);
        EXPECT_TRUE(messages.str().empty()) << messages.str();
    }

    TEST_F(SkinningGpuTest, CorrectDestinationPlaysWithoutAnInvalidTransitionSource) {
        const double destination = centroid(1);
        m_scene.render_items[0].animation = std::make_shared<const AnimationPlayback>(
            AnimationPlayback{.current = {.clip = "move", .time = 1, .loop = false},
                .previous = {.clip = "old_model_clip", .time = 0},
                .blend = 0.1f});
        for(int frame = 0; frame < 4; ++frame)
            EXPECT_NEAR(centroid(), destination, 1);
        consume_missing_clip_diagnostics("old_model_clip");
        EXPECT_EQ(m_scene.render_items[0].animation->previous.clip, "old_model_clip");
        EXPECT_EQ(m_scene.render_items[0].animation->blend, 0.1f);
    }

    TEST_F(SkinningGpuTest, HiddenViewResumesWithTheSamePausedPoseAcrossFrameSlots) {
        const double expected = centroid(1);
        const auto playback = m_scene.render_items[0].animation;
        auto& renderer = engine->get_renderer();
        ASSERT_TRUE(renderer.set_render_view({.visible = false}));
        for(int frame = 0; frame < 4; ++frame) {
            const auto prepared = render_frontend->prepare_frame();
            ASSERT_TRUE(prepared) << prepared.error();
            ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
            ASSERT_TRUE(render_frontend->render(m_scene));
        }
        ASSERT_TRUE(renderer.set_render_view({}));
        for(int frame = 0; frame < 6; ++frame)
            EXPECT_NEAR(centroid(), expected, 1);
        EXPECT_EQ(m_scene.render_items[0].animation, playback);
    }
    TEST_F(SkinningGpuTest, PresentedLocalPoseAndAdditiveReferenceReachGpuAcrossFrameSlots) {
        const double quarter = centroid(.25);
        const double three_quarters = centroid(.75);
        const auto data = m_source.animation;
        AnimationPlayback frame{.current = {.clip = "move", .time = 0, .loop = false}};
        auto first = sample_animation(*data, &frame);
        frame.current.time = 1;
        auto last = sample_animation(*data, &frame);
        ASSERT_TRUE(first);
        ASSERT_TRUE(last);
        auto interpolated = interpolate_animation_pose(*data, first.value(), last.value(), .25f);
        ASSERT_TRUE(interpolated);
        frame.pose_source = data;
        frame.pose = std::make_shared<const AnimationPose>(std::move(interpolated).value());
        const auto frozen = std::make_shared<const AnimationPlayback>(frame);
        m_scene.render_items[0].animation = frozen;
        for(int i = 0; i < 4; ++i)
            EXPECT_NEAR(centroid(), quarter, 1);
        frame.pose.reset();
        frame.current.time = .25;
        frame.layers = {{.sample = {.clip = "move", .time = 1, .loop = false},
            .weight = .5f,
            .mask = {"Bone"},
            .mode = AnimationLayer::Mode::Additive,
            .reference_clip = "move",
            .reference_time = 0}};
        m_scene.render_items[0].animation = std::make_shared<const AnimationPlayback>(frame);
        for(int i = 0; i < 4; ++i)
            EXPECT_NEAR(centroid(), three_quarters, 1);
        m_scene.render_items[0].animation = frozen;
        EXPECT_NEAR(centroid(), quarter, 1);
        EXPECT_TRUE(messages.str().empty()) << messages.str();
    }

    TEST_F(SkinningGpuTest, TwoBoneConstraintReachesSkinnedVerticesAndMatchesTargetRotation) {
        const double expected = centroid(.85);
        auto source = m_source;
        auto data = std::make_shared<AnimationData>();
        data->mesh_node = 0;
        data->nodes = {{"Mesh"}, {"hip", NO_ANIMATION_NODE, {.translation = {0, 1, 0}}},
            {"knee", 1, {.translation = {0, -.5f, .2f}}},
            {"Bone", 2, {.translation = {0, -.5f, -.2f}}}};
        data->evaluation_order = {0, 1, 2, 3};
        data->joints = {
            {.node = 1}, {.node = 2}, {.node = 3, .bounds = m_source.animation->joints[0].bounds}};
        source.animation = data;
        source.skin = std::vector<SkinInfluences>(3, {{2, 0, 0, 0}, {1, 0, 0, 0}});
        auto mesh = engine->get_render_resources().try_create_mesh(source);
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(engine->get_asset_registry().replace_asset(AssetHandle(10), mesh.value()));
        AnimationPlayback frame{.ik = {{.tip = 3,
                                    .target = {.35f, 0, 0},
                                    .pole = {0, 0, 1},
                                    .rotation = Math::Quat(1, 0, 0, 0)}},
            .pose_source = data};
        m_scene.render_items[0].animation = std::make_shared<const AnimationPlayback>(frame);
        for(int i = 0; i < 4; ++i)
            EXPECT_NEAR(centroid(), expected, 1);
        auto pose = sample_animation(*data, &frame);
        ASSERT_TRUE(pose) << pose.error();
        frame.pose = std::make_shared<const AnimationPose>(std::move(pose).value());
        m_scene.render_items[0].animation = std::make_shared<const AnimationPlayback>(frame);
        for(int i = 0; i < 4; ++i)
            EXPECT_NEAR(centroid(), expected, 1);
        EXPECT_TRUE(messages.str().empty()) << messages.str();
    }

    TEST_F(SkinningGpuTest, RetargetedLibraryKeepsTargetBindShapeAndAppliesSelectedMotionScale) {
        const double expected = centroid(.7);
        auto source = std::make_shared<AnimationData>(*m_source.animation);
        source->nodes[1].name = "source_bone";
        source->nodes[1].rest.translation.x = .1f;
        source->nodes[1].rest.rotation = Math::angle_axis(.7f, {0, 0, 1});
        const auto q = source->nodes[1].rest.rotation;
        source->clips[0].channels.push_back(
            {1, AnimationChannel::Path::Rotation, AnimationChannel::Interpolation::Linear, {0, 1},
                {{q.x, q.y, q.z, q.w}, {q.x, q.y, q.z, q.w}}});
        AnimationLibrary library;
        library.entries = {{"retargeted", AssetHandle(99), "move"}};
        library.sources = {{AssetHandle(99), source}};
        library.retargets = {{AssetHandle(99), {.nodes = {{"source_bone", "Bone"}},
                                                   .translations = {"source_bone"},
                                                   .translation_scale = .5f}}};
        auto bound = library.bind(*m_source.animation);
        ASSERT_TRUE(bound) << bound.error();
        AnimationPlayback frame{.current = {.clip = "retargeted", .time = 1, .loop = false},
            .clips = std::make_shared<const AnimationData>(std::move(bound).value()),
            .clips_target = m_source.animation};
        m_scene.render_items[0].animation = std::make_shared<const AnimationPlayback>(frame);
        for(int i = 0; i < 4; ++i)
            EXPECT_NEAR(centroid(), expected, 1);
        EXPECT_TRUE(messages.str().empty()) << messages.str();
    }

}

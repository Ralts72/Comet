#include "support/render_gpu_test.h"
#include "asset/data/mesh_data.h"
#include "render/material/material.h"
#include "asset/registry.h"
#include "render/resource/mesh.h"
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
                AnimationPlayback{.clip = "move", .time = time, .loop = false});
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
    TEST_F(SkinningGpuTest, GpuPoseMovesPixelsAndSharedMeshInstancesKeepIndependentPoses) {
        const double first = centroid(0);
        const double second = centroid(1);
        EXPECT_GT(second - first, 45);
        m_scene.render_items.push_back(m_scene.render_items.front());
        m_scene.render_items[1].entity_id = 2;
        m_scene.render_items[1].animation = std::make_shared<AnimationPlayback>(
            AnimationPlayback{.clip = "move", .time = 1, .loop = false});
        const double both = centroid(0);
        EXPECT_NEAR(both, (first + second) / 2, 1);
        m_scene.render_items.pop_back();
        EXPECT_NEAR(centroid(0), first, 1);
    }
    TEST_F(SkinningGpuTest, MissingClipUsesBindPoseAndReplacementInvalidatesPausedPose) {
        const double first = centroid(0);
        const double second = centroid(1);
        m_scene.render_items[0].animation = std::make_shared<const AnimationPlayback>(
            AnimationPlayback{.clip = "missing", .time = 1, .loop = false});
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
    TEST_F(SkinningGpuTest, CorrectDestinationPlaysWithoutAnInvalidTransitionSource) {
        const double destination = centroid(1);
        m_scene.render_items[0].animation =
            std::make_shared<const AnimationPlayback>(AnimationPlayback{.clip = "move",
                .time = 1,
                .loop = false,
                .previous_clip = "old_model_clip",
                .previous_time = 0,
                .blend = 0.1f});
        for(int frame = 0; frame < 4; ++frame)
            EXPECT_NEAR(centroid(), destination, 1);
        consume_missing_clip_diagnostics("old_model_clip");
        EXPECT_EQ(m_scene.render_items[0].animation->previous_clip, "old_model_clip");
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
}

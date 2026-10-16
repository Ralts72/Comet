#include "inspector/animation_preview.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "render/scene/scene_extractor.h"

#include <gtest/gtest.h>
#include <limits>

namespace CometEditor::Tests {
    class AnimationPreviewTest: public testing::Test {
    protected:
        std::shared_ptr<Comet::AnimationData> data = std::make_shared<Comet::AnimationData>();
        AnimationPreview preview;
        AnimationPreview::Target target{1, Comet::EntityUuid::generate()};

        void SetUp() override {
            data->mesh_node = 0;
            data->nodes = {{"Mesh"}, {"Joint"}};
            data->evaluation_order = {0, 1};
            data->joints = {{.node = 1, .bounds = Comet::BoundingBox::from_point({0, 0, 0})}};
            data->clips = {{.name = "move",
                .duration = 1,
                .channels = {{.node = 1,
                    .path = Comet::AnimationChannel::Path::Translation,
                    .interpolation = Comet::AnimationChannel::Interpolation::Linear,
                    .times = {0, 1},
                    .values = {{0, 0, 0, 0}, {2, 0, 0, 0}}}}}};
            ASSERT_TRUE(Comet::validate_animation(*data));
            preview.bind(target, data, "move");
        }
    };

    TEST_F(AnimationPreviewTest, RootMotionPreviewStaysInPlaceAndDoesNotMoveSceneContent) {
        preview.set_root_motion(Comet::AnimationRootMotion{});
        ASSERT_TRUE(preview.play());
        preview.advance(.4);
        const auto snapshot = preview.snapshot();
        ASSERT_TRUE(snapshot);
        auto pose = Comet::sample_animation(*data, snapshot.get());
        ASSERT_TRUE(pose) << pose.error();
        EXPECT_NEAR(pose.value().matrices[0][3].x, 0, .0001);
        preview.set_root_motion(std::nullopt);
        auto restored = Comet::sample_animation(*data, preview.snapshot().get());
        ASSERT_TRUE(restored);
        EXPECT_NEAR(restored.value().matrices[0][3].x, .8, .0001);
        EXPECT_TRUE(snapshot->root_motion);
    }

    TEST_F(AnimationPreviewTest, BlendPreviewUsesTheRuntimePeriodAndKeepsPhaseWhenWeightsChange) {
        auto second = data->clips.front();
        second.name = "long";
        second.duration = 2;
        second.channels.front().times.back() = 2;
        data->clips.push_back(second);
        Comet::AnimationBlend sample{
            .clip = "move", .loop = true, .blends = {{"long", .5f}}, .normalized_time = true};
        ASSERT_TRUE(preview.select_sample(sample, true));
        ASSERT_TRUE(preview.play());
        preview.advance(.75);
        ASSERT_TRUE(preview.snapshot());
        EXPECT_NEAR(preview.time(), .5, 1e-6);
        const auto captured = preview.snapshot();
        sample.blends.front().weight = .75f;
        ASSERT_TRUE(preview.select_sample(sample));
        EXPECT_NEAR(preview.time(), .5, 1e-6);
        EXPECT_EQ(captured->current.blends.front().weight, .5f);
        ASSERT_TRUE(preview.seek(.8));
        preview.advance(.5);
        EXPECT_NEAR(preview.time(), .8, 1e-6);
        preview.stop();
        ASSERT_TRUE(preview.select_sample(sample));
        EXPECT_FALSE(preview.snapshot());
        sample.blends.front().clip = "missing";
        EXPECT_FALSE(preview.select_sample(sample));
    }

    TEST_F(AnimationPreviewTest, SeekShowsTheLastKeyAndKeepsCapturedFramesImmutable) {
        EXPECT_FALSE(preview.snapshot());
        ASSERT_TRUE(preview.seek(0.5));
        const auto frozen = preview.snapshot();
        ASSERT_TRUE(frozen);
        EXPECT_FALSE(preview.playing());
        EXPECT_FALSE(frozen->current.loop);
        ASSERT_TRUE(preview.seek(5));
        auto pose = Comet::sample_animation(*data, preview.snapshot().get());
        ASSERT_TRUE(pose);
        EXPECT_DOUBLE_EQ(preview.time(), 1);
        EXPECT_FLOAT_EQ(pose.value().matrices.front()[3].x, 2);
        EXPECT_DOUBLE_EQ(frozen->current.time, 0.5);
        ASSERT_TRUE(preview.seek(-1));
        EXPECT_DOUBLE_EQ(preview.time(), 0);
        EXPECT_FALSE(preview.select_clip("missing"));
        EXPECT_EQ(preview.clip()->name, "move");
    }

    TEST_F(AnimationPreviewTest, LoopsOrStopsAndReusesPausedAndZeroSpeedFrames) {
        ASSERT_TRUE(preview.seek(0.25));
        const auto paused = preview.snapshot();
        preview.advance(0.5);
        EXPECT_EQ(preview.snapshot(), paused);
        ASSERT_TRUE(preview.play());
        preview.advance(1);
        EXPECT_DOUBLE_EQ(preview.time(), 0.25);
        EXPECT_EQ(preview.snapshot(), paused);
        ASSERT_TRUE(preview.set_speed(0));
        preview.advance(0.5);
        EXPECT_EQ(preview.snapshot(), paused);
        ASSERT_TRUE(preview.set_speed(2));
        preview.set_loop(false);
        preview.advance(1);
        EXPECT_DOUBLE_EQ(preview.time(), 1);
        EXPECT_FALSE(preview.playing());
        ASSERT_TRUE(preview.play());
        EXPECT_DOUBLE_EQ(preview.time(), 0);
        preview.stop();
        EXPECT_FALSE(preview.snapshot());
        EXPECT_FALSE(preview.playing());
    }

    TEST_F(AnimationPreviewTest, RebindingModelOrSceneStopsPreviewAndRejectsInvalidTime) {
        ASSERT_TRUE(preview.play());
        preview.advance(0.25);
        const auto frozen = preview.snapshot();
        preview.bind(target, data, "missing");
        EXPECT_EQ(preview.snapshot(), frozen);
        EXPECT_TRUE(preview.playing());
        const auto invalid = std::numeric_limits<double>::infinity();
        EXPECT_FALSE(preview.seek(invalid));
        EXPECT_FALSE(preview.set_speed(-1));
        EXPECT_FALSE(preview.set_speed(5));
        EXPECT_FALSE(preview.set_speed(std::numeric_limits<float>::quiet_NaN()));
        preview.advance(invalid);
        EXPECT_EQ(preview.snapshot(), frozen);

        auto replacement = std::make_shared<Comet::AnimationData>(*data);
        replacement->clips.front().name = "walk";
        preview.bind(target, replacement, "move");
        EXPECT_FALSE(preview.snapshot());
        EXPECT_FALSE(preview.playing());
        EXPECT_EQ(preview.clip()->name, "walk");
        EXPECT_EQ(data->clips.front().name, "move");
        ASSERT_TRUE(preview.play());
        ++target.scene_lifetime;
        preview.bind(target, replacement, "walk");
        EXPECT_FALSE(preview.snapshot());
        EXPECT_DOUBLE_EQ(frozen->current.time, 0.25);
    }

    TEST_F(AnimationPreviewTest, ScenePublicationAffectsRenderingAndNeverEntersSavedContent) {
        Comet::Scene scene;
        auto entity = scene.create_entity("Preview");
        entity.add_component<Comet::MeshRendererComponent>(
            Comet::AssetHandle{1}, Comet::AssetHandle{2});
        entity.add_component<Comet::AnimatorComponent>();
        const auto components = Comet::create_scene_component_registry();
        const Comet::SceneSerializer serializer(components);
        const auto content = serializer.serialize(scene);
        ASSERT_TRUE(content);
        preview.bind({scene.get_lifetime(), entity.get_uuid()}, data, "move");
        ASSERT_TRUE(preview.seek(0.5));
        scene.set_animation_playback(entity, preview.snapshot());
        const auto frozen = Comet::SceneExtractor::extract(scene);
        ASSERT_EQ(frozen.render_items.size(), 1u);
        EXPECT_EQ(frozen.render_items.front().animation, preview.snapshot());
        const auto revision = scene.get_render_revision();
        scene.set_animation_playback(entity, preview.snapshot());
        EXPECT_EQ(scene.get_render_revision(), revision);
        ASSERT_TRUE(preview.seek(1));
        scene.set_animation_playback(entity, preview.snapshot());
        EXPECT_DOUBLE_EQ(frozen.render_items.front().animation->current.time, 0.5);
        EXPECT_EQ(serializer.serialize(scene).value(), content.value());
        auto clone = serializer.clone(scene);
        ASSERT_TRUE(clone);
        EXPECT_FALSE(
            clone.value()->get_animation_playback(clone.value()->find_entity(entity.get_uuid())));
        Comet::SceneRuntime runtime;
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(scene.get_animation_playback(entity));
        ASSERT_TRUE(runtime.stop());
    }
}

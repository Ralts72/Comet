#include "animation/animation_data.h"
#include "animation/animation_system.h"
#include "asset/artifact/mesh_artifact.h"
#include "asset/import/mesh_importer.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "scene/component_registry.h"
#include "render/scene/scene_extractor.h"
#include "support/temporary_directory.h"
#include <gtest/gtest.h>
namespace Comet::Tests {
    namespace {
        std::filesystem::path default_character() {
            return std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY).parent_path()
                   / "engine/resources/characters/default.glb";
        }
        AnimationData simple_animation() {
            AnimationData data;
            data.mesh_node = 0;
            data.nodes = {{"Mesh"}, {"Joint"}};
            data.evaluation_order = {0, 1};
            data.joints = {{.node = 1, .bounds = BoundingBox::from_point({0, 0, 0})}};
            data.clips = {{.name = "move",
                .duration = 1,
                .channels = {{.node = 1,
                    .path = AnimationChannel::Path::Translation,
                    .interpolation = AnimationChannel::Interpolation::Linear,
                    .times = {0, 1},
                    .values = {{0, 0, 0, 0}, {2, 0, 0, 0}}}}}};
            return data;
        }
    }
    TEST(AnimationTest, ImportsRealDefaultCharacterAndEveryLocomotionClip) {
        const auto estimate = MeshImporter::working_bytes(default_character());
        ASSERT_TRUE(estimate);
        EXPECT_FALSE(MeshImporter{}.import(default_character(), estimate.value() - 1));
        auto imported = MeshImporter{}.import(default_character(), estimate.value());
        ASSERT_TRUE(imported) << imported.error();
        const auto& mesh = imported.value();
        ASSERT_TRUE(mesh.animation);
        EXPECT_EQ(mesh.animation->joints.size(), 65);
        EXPECT_EQ(mesh.skin.size(), mesh.vertices.size());
        EXPECT_EQ(mesh.animation->clips.size(), 7);
        ASSERT_TRUE(validate_animation(*mesh.animation));
        for(const auto& clip : mesh.animation->clips) {
            EXPECT_GT(clip.duration, 0);
            const AnimationPlayback playback{.clip = clip.name, .time = clip.duration * 0.4};
            auto sampled = sample_animation(*mesh.animation, &playback);
            ASSERT_TRUE(sampled) << clip.name << ": " << sampled.error();
            EXPECT_EQ(sampled.value().matrices.size(), 65);
            EXPECT_GT(sampled.value().bounds.maximum.y, sampled.value().bounds.minimum.y);
        }
    }
    TEST(AnimationTest, SamplesLoopsClampsAndBlendsWithoutChangingAsset) {
        auto data = simple_animation();
        ASSERT_TRUE(validate_animation(data));
        AnimationPlayback playback{.clip = "move", .time = 0.25};
        auto sampled = sample_animation(data, &playback);
        ASSERT_TRUE(sampled);
        EXPECT_NEAR(sampled.value().matrices[0][3].x, 0.5f, 0.0001f);
        playback.time = 1.25;
        EXPECT_NEAR(sample_animation(data, &playback).value().matrices[0][3].x, 0.5f, 0.0001f);
        playback.loop = false;
        EXPECT_NEAR(sample_animation(data, &playback).value().matrices[0][3].x, 2, 0.0001f);
        playback.previous_clip = "move";
        playback.previous_time = 0;
        playback.blend = 0.25f;
        EXPECT_NEAR(sample_animation(data, &playback).value().matrices[0][3].x, 0.5f, 0.0001f);
        EXPECT_EQ(data.nodes[1].rest.translation, Math::Vec3(0));
        playback.clip = "missing";
        EXPECT_FALSE(sample_animation(data, &playback));
        data.nodes[0].parent = 1;
        EXPECT_FALSE(validate_animation(data));
    }
    TEST(AnimationTest, AnimationAndWeightsSurviveArtifactRoundTrip) {
        auto imported = MeshImporter{}.import(default_character());
        ASSERT_TRUE(imported);
        TemporaryDirectory directory;
        MeshArtifact artifact{.handle = AssetHandle(91),
            .importer_version = MeshImporter::VERSION,
            .source_inputs = {.files = {{.relative_path = "default.glb", .size = 1, .hash = 1}}},
            .data = std::move(imported).value()};
        const auto file = directory.path() / "mesh.cache";
        ASSERT_TRUE(artifact.publish_atomic(file));
        auto loaded = MeshArtifact::load(file, artifact.handle);
        ASSERT_TRUE(loaded);
        ASSERT_TRUE(loaded->data.animation);
        EXPECT_EQ(loaded->data.animation->clips.size(), 7);
        EXPECT_EQ(loaded->data.skin.front().joints, artifact.data.skin.front().joints);
        EXPECT_EQ(loaded->data.skin.front().weights, artifact.data.skin.front().weights);
        AnimationPlayback playback{.clip = "run", .time = 0.15};
        auto before = sample_animation(*artifact.data.animation, &playback);
        auto after = sample_animation(*loaded->data.animation, &playback);
        ASSERT_TRUE(before);
        ASSERT_TRUE(after);
        EXPECT_EQ(before.value().matrices, after.value().matrices);
    }
    TEST(AnimationTest, PauseStepStopAndFrozenRenderSnapshotKeepPlaybackOutOfContent) {
        auto scene = std::make_shared<Scene>();
        auto entity = scene->create_entity("Animated");
        entity.add_component<MeshRendererComponent>(
            MeshRendererComponent{AssetHandle(1), AssetHandle(2)});
        entity.add_component<AnimatorComponent>();
        const auto registry = create_scene_component_registry();
        SceneSerializer serializer(registry);
        const auto content = serializer.serialize(*scene).value();
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<AnimationSystem>()));
        ASSERT_TRUE(runtime.start(*scene));
        ASSERT_TRUE(runtime.advance(0.1));
        auto frozen = SceneExtractor::extract(*scene);
        ASSERT_TRUE(frozen.render_items.front().animation);
        const double time = frozen.render_items.front().animation->time;
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_EQ(scene->get_animation_playback(entity)->time, time);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_GT(scene->get_animation_playback(entity)->time, time);
        EXPECT_EQ(frozen.render_items.front().animation->time, time);
        EXPECT_EQ(serializer.serialize(*scene).value(), content);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene->get_animation_playback(entity));
        EXPECT_FALSE(SceneExtractor::extract(*scene).render_items.front().animation);
    }
}

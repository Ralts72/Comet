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
#include <algorithm>
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
            const AnimationPlayback playback{
                .current = {.clip = clip.name, .time = clip.duration * 0.4}};
            auto sampled = sample_animation(*mesh.animation, &playback);
            ASSERT_TRUE(sampled) << clip.name << ": " << sampled.error();
            EXPECT_EQ(sampled.value().matrices.size(), 65);
            EXPECT_GT(sampled.value().bounds.maximum.y, sampled.value().bounds.minimum.y);
        }
    }
    TEST(AnimationTest, SamplesLoopsClampsAndBlendsWithoutChangingAsset) {
        auto data = simple_animation();
        ASSERT_TRUE(validate_animation(data));
        ASSERT_NE(data.find_clip("move"), nullptr);
        EXPECT_FALSE(data.find_clip("missing"));
        AnimationPlayback playback{.current = {.clip = "move", .time = 0.25}};
        auto sampled = sample_animation(data, &playback);
        ASSERT_TRUE(sampled);
        EXPECT_NEAR(sampled.value().matrices[0][3].x, 0.5f, 0.0001f);
        const auto skin_only = sample_animation(data, &playback, false);
        ASSERT_TRUE(skin_only);
        EXPECT_TRUE(skin_only.value().bone_matrices.empty());
        EXPECT_EQ(skin_only.value().matrices, sampled.value().matrices);
        playback.current.time = 1.25;
        EXPECT_NEAR(sample_animation(data, &playback).value().matrices[0][3].x, 0.5f, 0.0001f);
        playback.current.loop = false;
        EXPECT_NEAR(sample_animation(data, &playback).value().matrices[0][3].x, 2, 0.0001f);
        playback.previous.clip = "move";
        playback.previous.time = 0;
        playback.blend = 0.25f;
        EXPECT_NEAR(sample_animation(data, &playback).value().matrices[0][3].x, 0.5f, 0.0001f);
        EXPECT_EQ(data.nodes[1].rest.translation, Math::Vec3(0));
        playback.current.clip = "missing";
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
        AnimationPlayback playback{.current = {.clip = "run", .time = 0.15}};
        auto before = sample_animation(*artifact.data.animation, &playback);
        auto after = sample_animation(*loaded->data.animation, &playback);
        ASSERT_TRUE(before);
        ASSERT_TRUE(after);
        EXPECT_EQ(before.value().matrices, after.value().matrices);
    }

    TEST(AnimationTest, BlendSpaceInterpolatesNeighborsAndClampsWithoutExtrapolation) {
        const std::array points{AnimationBlendPoint{0, "idle"}, AnimationBlendPoint{4, "walk"},
            AnimationBlendPoint{6, "run"}};
        const auto idle = blend_animation_1d(points, -1);
        ASSERT_TRUE(idle);
        EXPECT_EQ(idle.value().clip, "idle");
        EXPECT_TRUE(idle.value().blends.empty());
        const auto between = blend_animation_1d(points, 5);
        ASSERT_TRUE(between);
        EXPECT_EQ(between.value().clip, "walk");
        EXPECT_EQ(between.value().blends.front().clip, "run");
        EXPECT_FLOAT_EQ(between.value().blends.front().weight, .5f);
        EXPECT_TRUE(between.value().normalized_time);
        EXPECT_EQ(blend_animation_1d(points, 20).value().clip, "run");
        EXPECT_EQ(blend_animation_1d(points, 4).value().clip, "walk");
        EXPECT_FLOAT_EQ(blend_animation_1d(points, 4).value().blends.front().weight, 0);
        EXPECT_FALSE(blend_animation_1d({}, 0));
        EXPECT_FALSE(blend_animation_1d(points, std::numeric_limits<float>::infinity()));
        auto invalid = points;
        invalid[1].threshold = 0;
        EXPECT_FALSE(blend_animation_1d(invalid, 1));
        invalid[1].threshold = -1;
        EXPECT_FALSE(blend_animation_1d(invalid, 1));
        invalid = points;
        invalid[1].threshold = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(blend_animation_1d(invalid, 1));
    }

    TEST(AnimationTest, BlendSpaceSynchronizesUnequalDurationsBeforeStateTransition) {
        auto data = simple_animation();
        auto other = data.clips.front();
        other.name = "slow";
        other.duration = 2;
        other.channels.front().times.back() = 2;
        other.channels.front().values.back().x = 8;
        data.clips.push_back(other);
        const std::array points{AnimationBlendPoint{0, "move"}, AnimationBlendPoint{4, "slow"}};
        AnimationPlayback playback{.current = blend_animation_1d(points, 1).value()};
        playback.current.time = .5;
        EXPECT_FLOAT_EQ(animation_duration(data, playback.current).value(), 1.25f);
        // 相同半周期分别采样到 1 和 4，先按混合空间权重求得 1.75。
        auto sampled = sample_animation(data, &playback);
        ASSERT_TRUE(sampled);
        EXPECT_NEAR(sampled.value().matrices[0][3].x, 1.75f, .0001f);
        playback.current.time = 1.5;
        EXPECT_NEAR(sample_animation(data, &playback).value().matrices[0][3].x, 1.75f, .0001f);
        playback.previous = {.clip = "move", .time = 0};
        playback.blend = .5f;
        EXPECT_NEAR(sample_animation(data, &playback).value().matrices[0][3].x, .875f, .0001f);
        playback.current.blends.front().clip = "missing";
        EXPECT_FALSE(sample_animation(data, &playback));
        EXPECT_FALSE(animation_duration(data, playback.current));
        playback.current.blends.front().weight = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(sample_animation(data, &playback));
        EXPECT_EQ(data.clips.front().duration, 1);
        EXPECT_EQ(data.clips.back().duration, 2);
        const std::array rest{AnimationBlendPoint{0, ""}, AnimationBlendPoint{4, "move"}};
        playback = {.current = blend_animation_1d(rest, 2).value()};
        playback.current.time = .5;
        EXPECT_FLOAT_EQ(animation_duration(data, playback.current).value(), 1);
        EXPECT_NEAR(sample_animation(data, &playback).value().matrices[0][3].x, .5f, .0001f);
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
        const double time = frozen.render_items.front().animation->current.time;
        const auto paused_playback = scene->get_animation_playback(entity);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_EQ(scene->get_animation_playback(entity)->current.time, time);
        EXPECT_EQ(scene->get_animation_playback(entity), paused_playback);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_GT(scene->get_animation_playback(entity)->current.time, time);
        EXPECT_EQ(frozen.render_items.front().animation->current.time, time);
        EXPECT_EQ(serializer.serialize(*scene).value(), content);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene->get_animation_playback(entity));
        EXPECT_FALSE(SceneExtractor::extract(*scene).render_items.front().animation);
    }

    TEST(AnimationTest, FinishedClipReusesPlaybackAndRestartsWhenLoopingIsEnabled) {
        const auto data = std::make_shared<const AnimationData>(simple_animation());
        Scene scene;
        auto entity = scene.create_entity("Animated");
        entity.add_component<MeshRendererComponent>(AssetHandle(1), AssetHandle(2));
        entity.add_component<AnimatorComponent>(
            AnimatorComponent{.clip = "move", .loop = false, .transition = 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([&](AssetHandle) { return data; })));
        ASSERT_TRUE(runtime.start(scene));
        for(int frame = 0; frame < 12; ++frame)
            ASSERT_TRUE(runtime.advance(0.1));
        const auto finished = scene.get_animation_playback(entity);
        ASSERT_TRUE(finished);
        EXPECT_DOUBLE_EQ(finished->current.time, 1);
        const auto revision = scene.get_render_revision();
        for(int frame = 0; frame < 3; ++frame) {
            ASSERT_TRUE(runtime.advance(0.1));
            EXPECT_EQ(scene.get_animation_playback(entity), finished);
            EXPECT_EQ(scene.get_render_revision(), revision);
        }
        entity.edit_component<AnimatorComponent>([](auto& animator) { animator.loop = true; });
        ASSERT_TRUE(runtime.advance(0.1));
        const auto looping = scene.get_animation_playback(entity);
        ASSERT_TRUE(looping);
        EXPECT_NE(looping, finished);
        EXPECT_NEAR(looping->current.time, 0.1, 0.00001);
        EXPECT_DOUBLE_EQ(finished->current.time, 1);
    }

    TEST(AnimationTest, ZeroSpeedReusesPlaybackAndClipEditsKeepCapturedFramesImmutable) {
        Scene scene;
        auto entity = scene.create_entity("Animated");
        entity.add_component<MeshRendererComponent>(AssetHandle(1), AssetHandle(2));
        entity.add_component<AnimatorComponent>(
            AnimatorComponent{.clip = "idle", .speed = 0, .transition = 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<AnimationSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        const auto frozen = scene.get_animation_playback(entity);
        ASSERT_TRUE(frozen);
        for(int frame = 0; frame < 4; ++frame) {
            ASSERT_TRUE(runtime.advance(0.1));
            EXPECT_EQ(scene.get_animation_playback(entity), frozen);
        }
        entity.edit_component<AnimatorComponent>([](auto& animator) {
            animator.clip = "walk";
            animator.transition = 0.2f;
        });
        ASSERT_TRUE(runtime.advance(0.1));
        const auto changed = scene.get_animation_playback(entity);
        ASSERT_TRUE(changed);
        EXPECT_NE(changed, frozen);
        EXPECT_EQ(changed->current.clip, "walk");
        EXPECT_EQ(changed->previous.clip, "idle");
        EXPECT_LT(changed->blend, 1);
        EXPECT_EQ(frozen->current.clip, "idle");
        ASSERT_TRUE(runtime.advance(0.1));
        const auto settled = scene.get_animation_playback(entity);
        ASSERT_TRUE(settled);
        EXPECT_NE(settled, changed);
        EXPECT_EQ(settled->blend, 1);
        EXPECT_TRUE(settled->previous.clip.empty());
        EXPECT_LT(changed->blend, 1);
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_EQ(scene.get_animation_playback(entity), settled);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_animation_playback(entity));
    }
    TEST(AnimationTest, BoneAttachmentSharesSkinPoseAndKeepsOffsetsSnapshotsAndStopIndependent) {
        auto data = std::make_shared<AnimationData>(simple_animation());
        data->joints[0].inverse_bind = Math::translate(Math::Mat4(1), {-.3f, 0, 0});
        Scene scene;
        auto model = scene.create_entity("Model");
        model.add_component<MeshRendererComponent>(
            MeshRendererComponent{AssetHandle{1}, AssetHandle{2}});
        model.add_component<AnimatorComponent>(AnimatorComponent{.clip = "move"});
        model.edit_transform([](auto& transform) {
            transform.translation.x = 10;
            transform.scale = Math::Vec3(2);
        });
        auto item = scene.create_entity("Held item");
        item.add_component<BoneAttachmentComponent>(BoneAttachmentComponent{"Joint"});
        item.add_component<MeshRendererComponent>(
            MeshRendererComponent{AssetHandle{3}, AssetHandle{2}});
        item.edit_transform([](auto& transform) { transform.translation.x = .2f; });
        ASSERT_TRUE(scene.set_parent(item, model));
        prepare_bone_attachment_sources(scene, [data](AssetHandle) { return data; });
        EXPECT_NEAR(scene.get_world_matrix(item)[3].x, 10.4, .0001);
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([data](AssetHandle) { return data; })));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(.25));
        const auto captured = scene.get_animation_playback(model);
        ASSERT_TRUE(captured->pose);
        EXPECT_EQ(captured->pose_source, data);
        EXPECT_NEAR(captured->pose->matrices[0][3].x, .2f, .0001);
        EXPECT_NEAR(captured->pose->bone_matrices[0][3].x, .5f, .0001);
        EXPECT_NEAR(scene.get_world_matrix(item)[3].x, 11.4f, .0001);
        const auto output = SceneExtractor{}.extract(scene);
        const auto found =
            std::ranges::find(output.render_items, item.get_id(), &RenderItem::entity_id);
        ASSERT_NE(found, output.render_items.end());
        EXPECT_EQ(found->model_matrix, scene.get_world_matrix(item));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_EQ(scene.get_animation_playback(model), captured);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_GT(scene.get_world_matrix(item)[3].x, 11.4f);
        EXPECT_EQ(captured->pose->bone_matrices[0][3].x, .5f);
        EXPECT_EQ(item.get_component<TransformComponent>().translation.x, .2f);
        const auto components = create_scene_component_registry();
        auto cloned = SceneSerializer(components).clone(scene);
        ASSERT_TRUE(cloned) << cloned.error();
        EXPECT_EQ(cloned.value()
                      ->find_entity(item.get_uuid())
                      .get_component<BoneAttachmentComponent>()
                      .bone,
            "Joint");
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_animation_playback(model));
        EXPECT_NEAR(scene.get_world_matrix(item)[3].x, 10.4f, .0001);
        item.edit_component<BoneAttachmentComponent>(
            [](auto& attachment) { attachment.bone = "missing"; });
        EXPECT_NEAR(scene.get_world_matrix(item)[3].x, 10.4f, .0001);
        ASSERT_TRUE(scene.clear_parent(item));
        EXPECT_NEAR(scene.get_world_matrix(item)[3].x, .2f, .0001);
    }
    TEST(AnimationTest, BoneAttachmentUsesInterpolatedParentAndModelReimportInvalidatesPose) {
        auto data = std::make_shared<AnimationData>(simple_animation());
        Scene scene;
        auto model = scene.create_entity();
        model.add_component<MeshRendererComponent>();
        model.add_component<AnimatorComponent>(AnimatorComponent{.clip = "move"});
        auto item = scene.create_entity();
        item.add_component<BoneAttachmentComponent>(BoneAttachmentComponent{"Joint"});
        ASSERT_TRUE(scene.set_parent(item, model));
        prepare_bone_attachment_sources(scene, [data](AssetHandle) { return data; });
        class MoveModel final: public System {
        public:
            explicit MoveModel(Entity target) : m_target(target) {}
            Result<void, Error> fixed_update(Scene&, const Context&) override {
                m_target.edit_transform([](auto& value) { value.translation.x += 1; });
                return Result<void, Error>::success();
            }

        private:
            Entity m_target;
        };
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<MoveModel>(model)));
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([data](AssetHandle) { return data; })));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(.025));
        auto frame = scene.get_animation_playback(model);
        ASSERT_TRUE(frame->pose);
        const auto expected =
            scene.get_render_transform(model).world_matrix * frame->pose->bone_matrices[0];
        EXPECT_EQ(scene.get_render_transform(item).world_matrix, expected);
        EXPECT_NE(scene.get_render_transform(item).world_matrix, scene.get_world_matrix(item));
        auto changed = std::make_shared<AnimationData>(*data);
        changed->clips[0].channels[0].values[1].x = 4;
        prepare_bone_attachment_sources(scene, [changed](AssetHandle) { return changed; });
        EXPECT_EQ(scene.get_animation_playback(model)->pose_source, changed);
        EXPECT_NE(scene.get_animation_playback(model)->pose, frame->pose);
        EXPECT_NEAR(scene.get_animation_playback(model)->pose->bone_matrices[0][3].x, .1, .0001);
        EXPECT_NEAR(frame->pose->bone_matrices[0][3].x, .05, .0001);
        prepare_bone_attachment_sources(scene, [](AssetHandle) { return nullptr; });
        EXPECT_EQ(scene.get_world_matrix(item), scene.get_world_matrix(model));
    }

    TEST(AnimationTest, AttachmentPreparationResolvesEachParentOnceAndTracksHierarchyChanges) {
        auto data = std::make_shared<AnimationData>(simple_animation());
        data->nodes[1].rest.translation.x = 3;
        Scene scene;
        auto first = scene.create_entity("First model");
        first.add_component<MeshRendererComponent>(AssetHandle{1}, AssetHandle{2});
        auto second = scene.create_entity("Second model");
        second.add_component<MeshRendererComponent>(AssetHandle{1}, AssetHandle{2});
        second.edit_transform([](auto& value) { value.translation.x = 10; });
        auto plain = scene.create_entity("No mesh");
        auto left = scene.create_entity("Left");
        left.add_component<BoneAttachmentComponent>(BoneAttachmentComponent{"Joint"});
        auto right = scene.create_entity("Right");
        right.add_component<BoneAttachmentComponent>(BoneAttachmentComponent{"Joint"});
        auto orphan = scene.create_entity("Orphan");
        orphan.add_component<BoneAttachmentComponent>(BoneAttachmentComponent{"Joint"});
        ASSERT_TRUE(scene.set_parent(left, first));
        ASSERT_TRUE(scene.set_parent(right, first));
        ASSERT_TRUE(scene.set_parent(orphan, plain));
        unsigned resolutions = 0;
        const AnimationSystem::Source source = [&](AssetHandle handle) {
            EXPECT_EQ(handle, AssetHandle{1});
            ++resolutions;
            return data;
        };
        prepare_bone_attachment_sources(scene, source);
        EXPECT_EQ(resolutions, 1u);
        EXPECT_FLOAT_EQ(scene.get_world_matrix(left)[3].x, 3);
        EXPECT_FLOAT_EQ(scene.get_world_matrix(right)[3].x, 3);
        const auto revision = left.get_component<WorldTransformComponent>().revision;
        prepare_bone_attachment_sources(scene, source);
        EXPECT_EQ(resolutions, 2u);
        static_cast<void>(scene.get_world_matrix(left));
        EXPECT_EQ(left.get_component<WorldTransformComponent>().revision, revision);
        ASSERT_TRUE(scene.set_parent(right, second));
        prepare_bone_attachment_sources(scene, source);
        EXPECT_EQ(resolutions, 4u);
        EXPECT_FLOAT_EQ(scene.get_world_matrix(right)[3].x, 13);
        ASSERT_TRUE(scene.clear_parent(left));
        prepare_bone_attachment_sources(scene, source);
        EXPECT_EQ(resolutions, 5u);
        EXPECT_FLOAT_EQ(scene.get_world_matrix(left)[3].x, 0);
        EXPECT_FLOAT_EQ(scene.get_world_matrix(orphan)[3].x, 0);
    }

}

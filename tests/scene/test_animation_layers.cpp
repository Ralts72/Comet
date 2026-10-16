#include "animation/animation_system.h"
#include "asset/serialization/animation_controller_serializer.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "inspector/animation_preview.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>

namespace Comet::Tests {
    namespace {
        AnimationData skeleton() {
            AnimationData data;
            data.mesh_node = 0;
            data.nodes = {{"mesh"}, {"legs"}, {"spine", 1}, {"hand", 2}};
            data.evaluation_order = {0, 1, 2, 3};
            for(uint32_t node : {1u, 2u, 3u})
                data.joints.push_back({.node = node, .bounds = BoundingBox::from_point({0, 0, 0})});
            for(int i = 0; i < 3; ++i) {
                AnimationClip clip{.name = std::string(1, char('a' + i)), .duration = float(i + 1)};
                for(uint32_t node : {1u, 2u, 3u})
                    clip.channels.push_back({node, AnimationChannel::Path::Translation,
                        AnimationChannel::Interpolation::Linear, {0, float(i + 1)},
                        {{float(i), 0, 0, 0}, {float(i + 1), 0, 0, 0}}});
                data.clips.push_back(std::move(clip));
            }
            return data;
        }
        AnimationController controller() {
            AnimationController data;
            data.parameters = {{"x", .25f}, {"y", .25f}, {"upper", .5f}};
            data.initial = "move";
            data.states = {{.name = "move",
                               .motions = {{0, "a", 0}, {1, "b", 0}, {0, "c", 1}},
                               .parameter = "x",
                               .parameter_y = "y"},
                {.name = "upper", .motions = {{0, "c"}}}};
            data.layers = {{"upper_body", "upper", 1, "upper", {"spine"}}};
            return data;
        }
    }
    TEST(AnimationLayersTest, TwoDimensionalWeightsAreContinuousExactAndBounded) {
        const std::vector<AnimationBlendPoint> points{{0, "a", 0}, {1, "b", 0}, {0, "c", 1}};
        const auto exact = blend_animation_2d(points, {1, 0});
        ASSERT_TRUE(exact);
        EXPECT_EQ(exact.value().clip, "b");
        EXPECT_TRUE(exact.value().blends.empty());
        const auto middle = blend_animation_2d(points, {.25f, .25f});
        ASSERT_TRUE(middle);
        ASSERT_EQ(middle.value().blends.size(), 2u);
        const auto data = skeleton();
        AnimationPlayback frame{.current = middle.value()};
        const auto pose = sample_animation(data, &frame);
        ASSERT_TRUE(pose) << pose.error();
        EXPECT_NEAR(pose.value().matrices[0][3].x, .6f, 1e-5f);
        EXPECT_NEAR(animation_duration(data, frame.current).value(), 1.6f, 1e-5f);
        for(auto p : {Math::Vec2{-100, 50}, Math::Vec2{100, -100}, Math::Vec2{.25001f, .25f}}) {
            auto mixed = blend_animation_2d(points, p);
            ASSERT_TRUE(mixed);
            frame.current = mixed.value();
            EXPECT_TRUE(sample_animation(data, &frame));
        }
        auto invalid = points;
        invalid[1] = invalid[0];
        EXPECT_FALSE(blend_animation_2d(invalid, {}));
        invalid = points;
        invalid[0].y = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(blend_animation_2d(invalid, {}));
        EXPECT_FALSE(blend_animation_2d(points, {NAN, 0}));
    }
    TEST(AnimationLayersTest, MasksCoverDescendantsAndLayersComposeInOrder) {
        const auto data = skeleton();
        AnimationPlayback frame{.current = {.clip = "a"}};
        frame.layers = {{{.clip = "c"}, .5f, {"spine"}}};
        auto pose = sample_animation(data, &frame);
        ASSERT_TRUE(pose);
        EXPECT_FLOAT_EQ(pose.value().matrices[0][3].x, 0);
        EXPECT_FLOAT_EQ(pose.value().matrices[1][3].x, 1);
        EXPECT_FLOAT_EQ(pose.value().matrices[2][3].x, 2);
        EXPECT_EQ(pose.value().bone_matrices, pose.value().matrices);
        frame.layers.push_back({{.clip = "a"}, 1, {"hand"}});
        pose = sample_animation(data, &frame);
        ASSERT_TRUE(pose);
        EXPECT_FLOAT_EQ(pose.value().matrices[2][3].x, 1);
        std::reverse(frame.layers.begin(), frame.layers.end());
        pose = sample_animation(data, &frame);
        ASSERT_TRUE(pose);
        EXPECT_FLOAT_EQ(pose.value().matrices[2][3].x, 2);
        frame.layers[0].mask = {"missing"};
        EXPECT_FALSE(sample_animation(data, &frame));
        frame.layers[0].mask = {"spine"};
        frame.layers[0].weight = NAN;
        EXPECT_FALSE(sample_animation(data, &frame));
        EXPECT_FLOAT_EQ(data.nodes[2].rest.translation.x, 0);
    }
    TEST(AnimationLayersTest, ControllerRoundTripPreservesLayersLibraryAndTwoAxes) {
        auto data = controller();
        data.library = AssetHandle(123);
        const AnimationControllerSerializer serializer;
        auto text = serializer.serialize(data);
        ASSERT_TRUE(text) << text.error();
        auto restored = serializer.deserialize(text.value());
        ASSERT_TRUE(restored) << restored.error();
        EXPECT_EQ(restored.value(), data);
        auto one_axis = data;
        one_axis.states[0].parameter_y.clear();
        one_axis.states[0].motions[2].threshold = 2;
        const auto saved_axis = serializer.serialize(one_axis);
        ASSERT_TRUE(saved_axis);
        const auto restored_axis = serializer.deserialize(saved_axis.value());
        ASSERT_TRUE(restored_axis);
        EXPECT_EQ(restored_axis.value(), one_axis);
        data.states[0].parameter_y = "missing";
        EXPECT_FALSE(data.validate());
        data = controller();
        data.layers[0].weight_parameter = "missing";
        EXPECT_FALSE(data.validate());
        data = controller();
        data.layers[0].state = "missing";
        EXPECT_FALSE(data.validate());
        data = controller();
        data.layers[0].mask = {"spine", "spine"};
        EXPECT_FALSE(data.validate());
    }
    TEST(AnimationLayersTest, LibraryRemapsNodeOrderAndRejectsIncompatibleRestOrHierarchy) {
        auto source = std::make_shared<AnimationData>(skeleton());
        AnimationLibrary library;
        library.entries = {{"shared", AssetHandle(12), "b"}};
        library.sources = {{AssetHandle(12), source}};
        auto target = skeleton();
        std::swap(target.nodes[1], target.nodes[2]);
        target.nodes[1].parent = 2;
        target.nodes[2].parent = NO_ANIMATION_NODE;
        target.nodes[3].parent = 1;
        target.evaluation_order = {0, 2, 1, 3};
        target.joints[0].node = 2;
        target.joints[1].node = 1;
        target.clips.clear();
        auto bound = library.bind(target);
        ASSERT_TRUE(bound) << bound.error();
        ASSERT_EQ(bound.value().clips.size(), 1u);
        EXPECT_EQ(bound.value().clips[0].channels[0].node, 2u);
        AnimationPlayback frame{.current = {.clip = "shared"}};
        auto pose = sample_animation(bound.value(), &frame);
        ASSERT_TRUE(pose);
        EXPECT_FLOAT_EQ(pose.value().matrices[2][3].x, 3);
        auto text = library.serialize();
        ASSERT_TRUE(text);
        auto restored = AnimationLibrary::deserialize(text.value());
        ASSERT_TRUE(restored);
        EXPECT_EQ(restored.value().entries, library.entries);
        target.nodes[1].rest.translation.x = 1;
        EXPECT_FALSE(library.bind(target));
        target = skeleton();
        target.nodes[3].parent = 1;
        EXPECT_FALSE(library.bind(target));
        target = skeleton();
        target.nodes[3].name = "spine";
        EXPECT_FALSE(library.bind(target));
        library.entries[0].clip = "gone";
        EXPECT_FALSE(library.bind(skeleton()));
    }
    TEST(AnimationLayersTest, RuntimeLayersKeepIndependentClocksAndFrozenSnapshotsAcrossPause) {
        auto data = std::make_shared<AnimationData>(skeleton());
        auto definition = std::make_shared<AnimationController>(controller());
        auto library = std::make_shared<AnimationLibrary>();
        for(const auto& clip : data->clips)
            library->entries.push_back({clip.name, AssetHandle(12), clip.name});
        library->sources = {{AssetHandle(12), data}};
        definition->library = AssetHandle(9);
        Scene scene;
        auto entity = scene.create_entity("actor");
        entity.add_component<MeshRendererComponent>();
        entity.add_component<AnimatorComponent>(AnimatorComponent{.controller = AssetHandle(4)});
        SceneRuntime runtime;
        ASSERT_TRUE(
            runtime.add_system(std::make_unique<AnimationSystem>([&](AssetHandle) { return data; },
                [&](AssetHandle) { return definition; }, [&](AssetHandle) { return library; })));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(.1));
        const auto frozen = scene.get_animation_playback(entity);
        ASSERT_TRUE(frozen);
        ASSERT_EQ(frozen->layers.size(), 1u);
        EXPECT_DOUBLE_EQ(frozen->layers[0].sample.time, .1);
        EXPECT_TRUE(frozen->clips);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_EQ(scene.get_animation_playback(entity), frozen);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        const auto stepped = scene.get_animation_playback(entity);
        EXPECT_GT(stepped->layers[0].sample.time, frozen->layers[0].sample.time);
        EXPECT_TRUE(sample_animation(*data, stepped.get()));
        auto replaced = std::make_shared<AnimationData>(*data);
        data = replaced;
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(scene.get_animation_playback(entity)->clips_target, data);
        EXPECT_FALSE(sample_animation(*data, frozen.get()));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_animation_playback(entity));
    }
    TEST(AnimationLayersTest, EditorPreviewUsesSameLibraryLayersAndPauseClock) {
        auto data = std::make_shared<AnimationData>(skeleton());
        auto library = std::make_shared<AnimationLibrary>();
        library->entries = {{"shared", AssetHandle(12), "b"}};
        library->sources = {{AssetHandle(12), data}};
        CometEditor::AnimationPreview preview;
        auto target = std::make_shared<AnimationData>(*data);
        target->clips.clear();
        preview.bind({1, EntityUuid::generate()}, target, "");
        ASSERT_TRUE(preview.set_library(library));
        ASSERT_TRUE(preview.select_sample({.clip = "shared"}));
        preview.set_layers({{{.clip = "shared"}, .5f, {"spine"}}});
        ASSERT_TRUE(preview.play());
        preview.advance(.1);
        auto snapshot = preview.snapshot();
        ASSERT_TRUE(snapshot);
        EXPECT_TRUE(sample_animation(*target, snapshot.get()));
        EXPECT_DOUBLE_EQ(snapshot->layers[0].sample.time, .1);
        preview.pause();
        preview.advance(1);
        EXPECT_EQ(snapshot, preview.snapshot());
        ASSERT_TRUE(preview.seek(1));
        EXPECT_DOUBLE_EQ(preview.snapshot()->layers[0].sample.time, 1);
    }
    TEST(AnimationLayersTest, PreviewLayerKeepsPhaseWhenBlendCrossesSamplePoints) {
        auto data = std::make_shared<AnimationData>(skeleton());
        CometEditor::AnimationPreview preview;
        preview.bind({1, EntityUuid::generate()}, data, "a");
        const std::vector<AnimationBlendPoint> points{{0, "a", 0}, {1, "b", 0}, {0, "c", 1}};
        auto first = blend_animation_2d(points, {0, 0});
        ASSERT_TRUE(first);
        preview.set_layers({{first.value(), 1, {"spine"}}});
        ASSERT_TRUE(preview.play());
        preview.advance(.25);
        const auto frozen = preview.snapshot();
        auto second = blend_animation_2d(points, {1, 0});
        ASSERT_TRUE(second);
        preview.set_layers({{second.value(), 1, {"spine"}}});
        EXPECT_DOUBLE_EQ(preview.snapshot()->layers[0].sample.time, frozen->layers[0].sample.time);
        EXPECT_EQ(preview.snapshot()->layers[0].sample.clip, "b");
        preview.stop();
        ASSERT_TRUE(preview.play());
        EXPECT_DOUBLE_EQ(preview.snapshot()->layers[0].sample.time, 0);
    }

    TEST(AnimationLayersTest, AdditiveLayerPreservesBaseAndAppliesMaskedReferenceDelta) {
        auto data = skeleton();
        data.clips.clear();
        const auto rotation = [](float angle) {
            const auto q = Math::angle_axis(angle, {0, 0, 1});
            return Math::Vec4(q.x, q.y, q.z, q.w);
        };
        const auto clip = [&](std::string name, float x, float angle, float scale) {
            return AnimationClip{.name = std::move(name),
                .channels = {{2, AnimationChannel::Path::Translation,
                                 AnimationChannel::Interpolation::Linear, {0}, {{x, 0, 0, 0}}},
                    {2, AnimationChannel::Path::Rotation, AnimationChannel::Interpolation::Linear,
                        {0}, {rotation(angle)}},
                    {2, AnimationChannel::Path::Scale, AnimationChannel::Interpolation::Linear, {0},
                        {{scale, scale, scale, 0}}}}};
        };
        data.clips = {clip("base", 5, Math::PI / 4, 2), clip("reference", 1, Math::PI / 6, 1),
            clip("overlay", 3, Math::PI / 2, 3)};
        AnimationPlayback frame{.current = {.clip = "base"},
            .layers = {{.sample = {.clip = "overlay"},
                .weight = .5f,
                .mask = {"spine"},
                .mode = AnimationLayer::Mode::Additive,
                .reference_clip = "reference"}}};
        auto pose = sample_animation(data, &frame);
        ASSERT_TRUE(pose) << pose.error();
        const auto& spine = pose.value().local_transforms[2];
        EXPECT_NEAR(spine.translation.x, 6, .0001);
        EXPECT_NEAR(spine.scale.x, 4, .0001);
        const auto expected = Math::angle_axis(Math::PI * 5 / 12, {0, 0, 1});
        EXPECT_NEAR(std::abs(glm::dot(spine.rotation, expected)), 1, .0001);
        EXPECT_EQ(pose.value().local_transforms[1].translation, Math::Vec3(0));
        // 末端骨骼没有动画通道，参考差值为零，不能再次加上绑定偏移。
        EXPECT_EQ(pose.value().local_transforms[3].translation, data.nodes[3].rest.translation);
        frame.layers[0].weight = 0;
        pose = sample_animation(data, &frame);
        ASSERT_TRUE(pose);
        EXPECT_NEAR(pose.value().local_transforms[2].translation.x, 5, .0001);
        frame.layers[0].weight = 1;
        frame.layers[0].reference_clip = "missing";
        EXPECT_FALSE(sample_animation(data, &frame));
        frame.layers[0].reference_clip.clear();
        data.nodes[2].rest.scale.x = 0;
        EXPECT_FALSE(sample_animation(data, &frame));
    }

    TEST(
        AnimationLayersTest, AdditiveReferenceTimeRoundTripsAndRuntimePreviewShareLayerEvaluation) {
        auto definition = std::make_shared<AnimationController>(controller());
        auto& layer = definition->layers[0];
        layer.mode = AnimationLayer::Mode::Additive;
        layer.reference_clip = "c";
        layer.reference_time = .25f;
        const AnimationControllerSerializer serializer;
        auto text = serializer.serialize(*definition);
        ASSERT_TRUE(text) << text.error();
        auto restored = serializer.deserialize(text.value());
        ASSERT_TRUE(restored) << restored.error();
        EXPECT_EQ(restored.value(), *definition);
        auto invalid_text = text.value();
        invalid_text.replace(invalid_text.find("additive"), 8, "unknown");
        EXPECT_FALSE(serializer.deserialize(invalid_text));
        auto sampled_layer = definition->sample_layer(layer, {}, {0, 0});
        ASSERT_TRUE(sampled_layer);
        EXPECT_FLOAT_EQ(sampled_layer.value().weight, .5f);
        EXPECT_FLOAT_EQ(sampled_layer.value().reference_time, .25f);
        auto data = std::make_shared<AnimationData>(skeleton());
        Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<MeshRendererComponent>();
        entity.add_component<AnimatorComponent>(AnimatorComponent{.controller = AssetHandle(4)});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([data](AssetHandle) { return data; },
                [definition](AssetHandle) { return definition; })));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(.1));
        const auto frame = scene.get_animation_playback(entity);
        ASSERT_TRUE(frame && frame->layers.size() == 1);
        EXPECT_EQ(frame->layers[0].mode, AnimationLayer::Mode::Additive);
        EXPECT_EQ(frame->layers[0].reference_clip, "c");
        CometEditor::AnimationPreview preview;
        preview.bind({1, EntityUuid::generate()}, data, "a");
        ASSERT_TRUE(preview.select_sample(frame->current));
        preview.set_layers(frame->layers);
        auto expected = sample_animation(*data, frame.get());
        auto actual = sample_animation(*data, preview.snapshot().get());
        ASSERT_TRUE(expected);
        ASSERT_TRUE(actual);
        EXPECT_EQ(actual.value().matrices, expected.value().matrices);
        layer.reference_time = -1;
        EXPECT_FALSE(definition->validate());
        ASSERT_TRUE(runtime.stop());
    }

}
